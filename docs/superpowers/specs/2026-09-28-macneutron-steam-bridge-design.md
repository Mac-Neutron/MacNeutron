# MacNeutron — Sub-project 2: Steam API Bridge

- **Date:** 2026-09-28
- **Status:** Approved 2026-09-28; amended the same day (see "Amendment") and on 2026-10-12 for Proton 11 parity (see
  "Amendment 2", which replaces the sections it lists)
- Superseded in part by `2026-10-04-macneutron-arm64-release-design.md` (the Rosetta runtime, GPTK, DXVK and the x86_64 DXMT build were removed in 0.1.0).
- **Builds on:** `2026-09-27-macproton-runtime-design.md` (architecture, runtime, launcher) and `2026-09-27-macneutron-app-design.md` (app, Steam Play mode).
- **Scope:**
  - **In:** our own `steam.exe`; wiring the runtime's Steam client bridge into every prefix; launcher integration; getting `steam.exe` from the app into the tool folder; a developer probe; acceptance.
  - **Out:** building or patching `lsteamclient` (the runtime provides it, §2.8; since Amendment 2 it is built in-tree
    with three patches, D-13); the Steam overlay; Easy Anti-Cheat and other anti-cheat runtimes; VR (`vrclient`); the repo's own license (§11).

## Amendment (2026-09-28, while planning)

The approved design planned to build Proton's `lsteamclient` for macOS ourselves (fetch Proton, build winecx, patch, bundle, ship under the Steamworks SDK license). While planning, §2.8 turned up: **the pinned runtime already ships a macOS build of `lsteamclient`**, ported by the runtime's maintainer, including the exact patch we planned. So:

- Dropped: fetching Proton and winecx, building `lsteamclient`, our patches, bundling it in the app, the bridge version file, and the licensing decision (the runtime's distributor carries it).
- Kept: `steam.exe`, the prefix layout, launcher integration, the probe, acceptance.
- Changed: the Windows helpers are built with Homebrew's `mingw-w64` (already required by `Tests/Smoke`), not llvm-mingw, so there are no downloads.

Amendment 2 reverses the lsteamclient and toolchain points: lsteamclient is built in-tree, and the helpers use the pinned
llvm-mingw.

## Amendment 2 (2026-10-12): Proton 11 parity

Specs and plans:
- spec: `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md`;
- plans: `docs/superpowers/plans/2026-10-12-macneutron-valve-steamclient.md` (P0-1) and
  `docs/superpowers/plans/2026-10-12-macneutron-parity-s-drive-steam-exe.md` (P0-3, P1-1, rulings R1-R20).

This amendment replaces:
- §1's decisions on the client bridge, the helper, the toolchain and 32-bit games;
- §2.8's and §3's runtime facts, including §3's Windows-path line;
- §4.1's source of lsteamclient, and §4.2;
- §5's toolchain;
- §7's command and account lines;
- §8's account, escape-hatch and crash rows;
- §10's `ActiveUser` row.

§9 is rewritten in place for it, and smaller stale facts in §6, §7, §8 and §11 (paths, log channels, the licence) are
corrected where they stand. Each replaced passage is struck or marked where it stands. The rest stands.

The maintainer's rule is that the Steam connection does what Proton 11 does, and each difference below carries its
reason.

**Facts today.**
- **lsteamclient** is built in-tree from Proton's `proton_11.0` (`wine-arm64/deps.pins`), as an ARM64X Wine builtin,
  with three macOS patches (D-13).
- **steam.exe** is an arm64 PE built with llvm-mingw (`make bridge`). It is a GUI program, as Proton's is.
- **steamclient64.dll** in the prefix is Valve's signed DLL from the app's download when the runtime has Wine 0036, and
  lsteamclient otherwise (D-10).

**Architecture** (replaces §3's diagram):

```
Mac Steam ──Play──▶ macneutron launch waitforexitandrun <the game's macOS path> <args>
   │ prefix: dosdevices/s: → the game's Steam library (Proton's setup_dir_drive, PROTON_SET_GAME_DRIVE=0 turns it off)
   │         C:\windows\system32\steam.exe and C:\Program Files (x86)\Steam\steam.exe (our helper)
   │         C:\Program Files (x86)\Steam\steamclient64.dll (Valve's signed DLL, or lsteamclient)
   ▼
wine C:\windows\system32\steam.exe /…/steamapps/common/<Game>/Game.exe <args>
   │ Proton's steam.exe: Steam set up for a game process (SteamGameId); the path becomes S:\steamapps\common\<Game>\…;
   │ ShellExecute for what isn't an .exe; waits for Wine's shutdown event; restarts the game when it asks
   ▼
game ─▶ steam_api64.dll ─▶ steamclient64.dll ─(Wine 0036)▶ lsteamclient.dll ─▶ lsteamclient.so ─▶ Mac steamclient.dylib
```

**steam.exe** (replaces §4.2): a close port of Proton 11's `steam_helper/steam.c`, under Valve's BSD-3 notice.
- **Command handler, first.** `steam://` URLs and arguments starting with `-` are commands:
  - `-- steam://rungameid/<its SteamGameId>` (or `launch`) restarts the game in place;
  - anything else goes to Mac Steam (D-12).
- **A game process** (SteamGameId set, empty included) gets, in Proton's order:
  - Steam's named events;
  - the desktop window, then the hidden `vguiPopupWindow` "Steam" and "SteamVR Status" windows;
  - `ActiveProcess\pid` and the keys Valve's wine.inf writes (D-26), and `HKCR\steam`;
  - `SteamPath` and `ValvePlatformMutex`, its own path from `GetModuleFileNameW`;
  - `Steam\config` and `steamapps\libraryfolders.vdf`;
  - `PROTON_WAIT_ATTACH`, and vrclient (absent on our runtime);
  - the SteamStub start answer (DIPC);
  - Wine's system-process status, which leads to the shutdown-event wait and the restart event.
- **Starting the program** (Proton's `run_process`):
  - a macOS path becomes its DOS path (`S:` in a library);
  - a console program gets a console of its own;
  - a target that isn't an `.exe` goes through ShellExecute, with the EA workarounds;
  - Source SDK's `-game` path is converted, quoted or not, as UTF-8 (D-34).
- **Exit code:** 0 for a command and for a ShellExecute target; 1 if the program can't be started; otherwise the last
  child's code. `STILL_ACTIVE` is possible if Wine's shutdown event comes first, as in Proton.

**Launcher** (replaces §7's command and account lines):
- The command is `wine C:\windows\system32\steam.exe <the game's macOS path> <args…>`. Three games (SteamGameId 311210,
  1985810, 1549250) run the Steam folder's copy, as in Proton.
- No account variable: Proton writes no `ActiveUser`, and steam.exe deletes the one earlier builds wrote.
- Every preparation links `s:`, with the bridge or without; Proton's messages go to launcher.log.
- The `steamapps` check reads the name as Proton's Python `os.path.realpath` gives it: it follows the library entry's
  links by their text, hop by hop (`lstat`, `readlink`), and keeps the given spelling of a part that isn't a link, not
  the on-disk case Darwin's `realpath(3)` returns. So a legacy on-disk `SteamApps` folder keeps `S:\steamapps` when Mac
  Steam passes it as `…/steamapps`, or when the entry is a link whose text spells it `steamapps`. This is parity, not a
  deviation.

**Errors** (amends §8):
- The account rows go.
- `MACNEUTRON_NO_STEAM_BRIDGE=1`, or a missing bridge file, removes the client DLLs but keeps `steam.exe`, so the
  game's `steam://` links, except its own restart link, still reach Mac Steam (D-27).
- steam.exe never clears its pid, as Proton's (R4): after a crash, or a steam.exe that fails to start its program, the
  pid is that steam.exe's, dead, until the next game's steam.exe registers its own.
- The S: drive applies with the bridge or without. `/usr/bin/env PROTON_SET_GAME_DRIVE=0 %command%` shows the game its
  `Z:` path again.

### Deviations from Proton 11 (each with its reason)

**Launching the tool**
- **D-1:** Steam runs `/bin/macneutron launch %verb%`, an arm64 Mach-O, not `/proton`: a clean Mac has no Python.
- **D-2:** No `require_tool_appid` and no `use_sessions`: macOS has no Steam Linux Runtime or pressure-vessel.
  `STEAM_COMPAT_MOUNTS` and the runtime adverb have no consumer.
- **D-3:** Compat tools apply only with `@sSteamCmdForcePlatformType linux` in the bundle's `steam_dev.cfg`. A second
  tool, `macneutron-native`, keeps Mac builds mounted.
- **D-4:** A local tool (AppID 0) installed and updated by MacNeutron.app: only Valve can publish a tool app.
- **D-5:** After the game, a second `wineserver -w` keeps Steam's "running" state until the prefix is empty: Mac Steam
  has no subprocess reaper.
- **D-6:** SIGTERM and SIGINT run `wineserver -k` for Steam's Stop button, for the same reason.
- **D-7:** `STEAM_COMPAT_CLIENT_INSTALL_PATH` is the Steam bundle's MacOS folder, which holds `steamclient.dylib`, not the
  Steam root. The Steam root's library and appcache paths come from `SteamLocation` instead.
- **D-8:** `libraryfolders.vdf` leaves out Proton's entry 0, the client folder: on macOS it is no library.
- **D-9:** Launch options run without a shell, so `VAR=x %command%` must be `/usr/bin/env VAR=x %command%`. Otherwise
  Steam fails with OS Error 260.

**Steam client bridge**
- **D-10:** `steamclient64.dll` falls back to lsteamclient when no Valve DLL is cached (first launch offline, or after
  Remove): Mac Steam ships no `legacycompat`.
- **D-11:** Wine 0036 writes its trampolines PE-side (RW, then restore), not through Unix `mprotect(RWX)`: Apple Silicon
  refuses W^X violations. Two more differences from Proton's loader:
  - lsteamclient.dll is loaded from `system32` only;
  - Proton also points the genuine DLL's entry point at lsteamclient's (`steamclient_setup_trampolines`); 0036 doesn't,
    and relies on `LDR_DONT_CALL_DLLMAIN`, which Proton sets too: the DLL's DllMain and TLS callbacks are skipped,
    unless no export was redirected.
- **D-12:** `steam://` URLs and steam.exe's commands go to Mac Steam through winebrowser, which runs `open(1)`. Proton
  runs the Linux client's `steam-runtime-steam-remote` or `steam` binary, which macOS doesn't have, with steam.exe's
  arguments on its command line (`steam -applaunch <id> <args>`). Mac Steam has no command line. Our additions:
  - a leading `--` is dropped;
  - `-applaunch <id>` with nothing after the id becomes `steam://rungameid/<id>`;
  - `-applaunch <id>` with arguments becomes Valve's documented `steam://run/<id>//<arguments>/`. Every following
    argument is taken and quoted as a Windows command line spells it: one that is empty or holds a space, tab or `"`
    goes in double quotes, a `"` becomes `\"`, and backslashes before a `"` or the closing quote are doubled. The
    arguments are joined by spaces and percent-encoded as UTF-8: `A-Z a-z 0-9` and `- _ . ~ + : , = @ ! $ ' ( ) * ; &`
    stay literal, and everything else, space, `"` and `/` included, becomes `%XX`. A `/`, `?`, `#` or `%` left in an
    argument would change the URL's structure (Steam splits `run/<id>/<language>/<arguments>` on `/`, and decodes
    `%XX`), and a `"` must survive. The form follows common usage (`steam://run/730//+connect%20<server>`). Example:
    `-applaunch 480 +connect "a b" -x` gives `steam://run/480//+connect%20%22a%20b%22%20-x/`. How Mac Steam handles
    the arguments, including whether it asks the player to confirm arguments that come from a link, is unverified (the
    maintainer observes it); Proton's Linux command line launches without asking;
  - no arguments open `steam://open/main`;
  - any other `-` option, including one before `-applaunch`, is logged and dropped;
  - the URL comes from the wide command line (`CommandLineToArgvW`), not the C runtime's ANSI argv, and the winebrowser
    command is built at any length, so a long link or one with letters outside the Windows code page reaches Mac Steam
    whole. Proton passes the C runtime's ANSI argv, which loses such letters;
  - every failure is logged through Wine's debug output (ntdll's `__wine_dbg_output`), prefixed `steam.exe: ` (stderr
    outside Wine): parity with Proton's `WINE_ERR`; our `ERR` was `fprintf` to the Windows stderr handle before. A
    steam.exe that a game's `steam://` link starts through `HKCR\steam` gets a new console and no Windows standard
    handles from Wine's shell32, and still logs to Wine's Unix stderr, the game log. Lines are formatted wide and
    written as UTF-8, so a letter above U+00FF no longer cuts one short.
- **D-13:** lsteamclient patches:
  - 0003: `Steam_IsKnownInterface` and `Steam_NotifyMissingInterface` are optional, because Mac `steamclient.dylib`
    doesn't export them;
  - 0001: native key codes are 0 (no X11 keysyms);
  - 0001 and 0002: `-DNOMINMAX` and `-lc++` for the build.
- ~~D-13a~~: removed. `ValvePlatformMutex` is Proton's now; Wine reports `C:/windows/system32/steam.exe`.
- **D-14:** No i386 Valve files, no 32-bit lsteamclient, and SteamService.exe isn't at `Steam\steam.exe` until WoW64
  exists and the helper is a builtin. Until then that path holds our helper. The three games above get only the
  location, not Proton's 32-bit helper process. For the same reason there is no `steam.exe=b`: Proton routes every run
  of any `steam.exe` to its builtin helper (Locoland, 352130, exempt), but our helper isn't a Wine builtin, and with
  `=b` Wine would refuse it and break every launch. A game or launcher reaches our helper only at our two copies,
  `C:\windows\system32\steam.exe` and `C:\Program Files (x86)\Steam\steam.exe`; any other `steam.exe` runs as itself.

**Platform**
- **D-15:** msync instead of fsync and ntsync, and DXMT on Metal instead of DXVK and vkd3d (macOS has no futex, ntsync or
  Vulkan). The Proton graphics and sync variables don't apply: of Proton's variables, the launcher reads only
  `PROTON_SET_GAME_DRIVE`, and steam.exe only `PROTON_HIDE_PROCESS_WINDOW` and `PROTON_WAIT_ATTACH`.
- **D-16:** Install scripts don't run: Mac Steam has no `legacycompat/iscriptevaluator.exe`, and our runtime can't run
  the i386 redistributable installers (the launcher skips a 32-bit `run` target). Revise after the P1-4 probe.
- **D-17:** Mac Steam's `LC_ALL` still passes through (P1-2, not yet). P1-2 sets `LC_ALL` from `HOST_LC_ALL`, or drops
  it when that is empty, as Proton does; with `LC_ALL` dropped the locale comes from CoreFoundation (the Mac's
  settings), because Mac Steam sends no `LANG`. Four Steam languages (Arabic, Latin American Spanish, Thai, Vietnamese)
  fall back to the Mac's locale, because macOS lacks those locale names.
- **D-18:** The overlay is still to be designed (P1-3 spike).

**Logs and settings**
- **D-19:** Logs are `~/Library/Logs/MacNeutron/steam-<appid>.log`, appended and rotated past 50 MB, with account values
  and secrets redacted. Per-game settings come from the app, not `user_settings.py`.
- **D-20:** `PROTON_CRASH_REPORT_DIR` isn't ported yet (P2-7). `destroyprefix` isn't implemented: the Mac client never
  sends it (P2-10).
- **D-21:** `HKCU\Software\Wine\X11 Driver\EmulateModeset=Y`: fullscreen mode changes stay virtual, so the Mac display is
  never switched.

**steam.exe** (plan rulings)
- **D-22:** The game is started with inherited handles; Proton inherits none. In our Wine a child started without
  inheritance gets no standard handles at all, and the game's own output is what MacNeutron's game log holds.
- **D-23:** Two DRM details (R5):
  - the SteamStub semaphores are created before the game starts, where Proton creates them just after: the stub can
    open them as soon as it runs;
  - the ack event is looked up each round in `\Sessions\1\BaseNamedObjects`, the directory our Wine uses.
- **D-24:** The restart event is opened with `SYNCHRONIZE | EVENT_MODIFY_STATE`; Proton opens it with `SYNCHRONIZE`
  alone, which our Wine refuses `SetEvent` (R6; Valve's own precedent is db9e6ffb).
- **D-25:** The restart loop is Proton's, without its precedence slip, which read a failed wait as a restart (R7).
- **D-26:** `HKCR\steam` (Proton's command) and the Steam keys of Valve's wine.inf (`[SteamClient64]` and the lines
  before it) are written by steam.exe for a game process: our prefix isn't built from Valve's wine.inf. A prefix that
  has never run a game with the bridge has no `steam://` handler (R9). Where steam.exe differs from wine.inf:
  - no `ActiveProcess\SteamPath`;
  - `ActiveProcess\SteamClientDll` only when `steamclient.dll` exists, and no 32-bit client is deployed (D-14); wine.inf
    writes it always;
  - `HKCR\steam` also gets an empty `URL Protocol`, which wine.inf lacks (R9 kept it from the earlier winebrowser
    handler).

  The first two, and seeding the keys at prefix creation, are the parity spec's P2-5.
- **D-27:** `MACNEUTRON_NO_STEAM_BRIDGE=1` keeps steam.exe, for the `steam://` handler (R10). A game's link to restart
  itself (`steam://rungameid/<its id>` or `steam://launch/<its id>`) isn't forwarded to Mac Steam then: no game
  steam.exe holds the restart event, so the `HKCR\steam` steam.exe logs `Restart event not found` and stops, as
  Proton's does.
- **D-28:** A non-link at `dosdevices/s:` is left alone and noted, whether the drive is on or off (R15). Proton's script
  raises there (`os.readlink`) only when the drive is on and has a target. With the drive off, or no target, it deletes
  a regular file at `s:` (`os.remove`) and raises on a folder. A player's own file is never deleted here.
- **D-29:** EA, two gaps:
  - Valve-Wine's desktop close timeout (index 1000) isn't in our Wine, so the call only logs an error (R14, not ported
    yet);
  - on `SE_ERR_NOASSOC` the EA app is not uninstalled, because install scripts never run here to reinstall it (R19,
    D-16).
- **D-30:** `t:` (steamdrive) and `STEAM_COMPAT_CONFIG`'s `cmdlineappend` aren't ported yet (R17).
- **D-31:** `PROTON_WAIT_ATTACH` ends only for a Windows debugger: our Wine has no Unix-debugger query (R18).
- **D-32:** Hardening over Proton:
  - `libraryfolders.vdf` skips empty library items, where Proton copies into an uninitialised buffer;
  - it reads the library paths as UTF-8, where Proton's `getenv` mangles a non-ASCII path, which stalled Wine's lookup;
  - a Unix path Wine can't convert is run as given;
  - an early failure exits 1.
- **D-33:** No GamingRepair mark (it waits for P1-4) and no `steamclient_init_registry` (P1-2) yet (R13). Until it is
  ported, steam.exe writes that call's registry keys itself, for a game process: `HKCU\Software\Valve\Steam\language` from
  `MACNEUTRON_STEAM_LANGUAGE`, and, for a nonzero `SteamAppId`, `Steam\Apps\<SteamAppId>` with `Installed` 1,
  `Running` 1 and `Updating` 0.
- **D-34:** Source SDK's `-game` path (Proton's `fixup_cmdline_paths`), hardening over Proton, which converts only an
  unquoted path and narrows each character to a byte:
  - Wine quotes an argument holding a space, and every path under `~/Library/Application Support/Steam` has one, so a
    quoted path is converted too, and its quotes are kept. A trailing `\` before the closing quote is doubled:
    `wine_get_dos_file_name` keeps a trailing separator, and Windows reads `\"` as a quote;
  - the path is converted as UTF-8 (`CP_UNIXCP`, as in `run_process`): the byte-narrowed form of a non-ASCII path is
    what stalled Wine's lookup (D-32);
  - still only behind Proton's `\Source SDK Base` gate, and only the first `-game`.

## 1. Goal

A Windows game started from native macOS Steam through MacNeutron can use the Steam API: `SteamAPI_Init` succeeds, and ownership/DRM checks, achievements, cloud saves, inventory and auth session tickets reach the user's running Mac Steam client. Games stop reporting "Steam unavailable".

**Done when** the acceptance run in §9 passes. This completes the v1 goal in the runtime spec: "a Windows-only Steam game that uses the Steam API (DRM, achievements) installs and launches from native macOS Steam's Play button".

### Decisions

| Decision | Choice |
|---|---|
| Steam client bridge | ~~The runtime's own `lsteamclient` (§2.8); we neither build nor ship it~~ Amendment 2: `lsteamclient` built in-tree and shipped in `wine.app`; Valve's signed `steamclient64.dll` in the prefix when the runtime has Wine 0036 (D-10) |
| "Steam is running" | ~~Our own `steam.exe` (open source, not Proton's `steam_helper`)~~ Amendment 2: our `steam.exe`, a close port of Proton's `steam_helper/steam.c` |
| Windows helper toolchain | ~~Homebrew `mingw-w64` (`x86_64-w64-mingw32-gcc`), as `Tests/Smoke` already uses~~ Amendment 2: the pinned llvm-mingw, arm64 (`make bridge`) |
| Shipping `steam.exe` | Inside `MacNeutron.app/Contents/Resources/` (codesign accepts only Mach-O code in `Contents/Helpers`), installed into the tool folder with the launcher |
| 32-bit games | ~~`steamclient.dll` is copied whenever the runtime has an i386 build; 32-bit is recorded as untested unless a 32-bit Steam game is available~~ Amendment 2: no 32-bit client (D-14) |
| Escape hatch | `MACNEUTRON_NO_STEAM_BRIDGE=1` in a game's launch options starts the game directly; `steam.exe` stays in the prefix (D-27) |

## 2. Evidence (verified 2026-09-28)

1. macOS Steam's `Steam.AppBundle/Steam/Contents/MacOS/steamclient.dylib` is universal (`x86_64 arm64`), and so are its dependencies `libtier0_s.dylib`, `libvstdlib_s.dylib` and `libaudio.dylib`.
2. Its x86_64 slice exports `CreateInterface`, `Steam_BGetCallback`, `Steam_GetAPICallResult`, `Steam_FreeLastCallback`, `Steam_ReleaseThreadLocalMemory` and the other `Steam_*` entry points, but **not** `Steam_IsKnownInterface` or `Steam_NotifyMissingInterface`.
3. Proton's `lsteamclient` (`proton_11.0`, `5b89db9`) has an `#ifdef __APPLE__` path that loads `$STEAM_COMPAT_CLIENT_INSTALL_PATH/steamclient.dylib`, but requires the two functions above.
4. `lsteamclient/LICENSE` is the Steamworks SDK license.
5. The runtime `runtime-v4.7.3` was built from `dappermint/winecx` `e0aa380780b73e20fabcfe78fd42713b94929a53`.
6. SMITE 2 ships `Engine/Binaries/ThirdParty/Steamworks/Steamv157/Win64/steam_api64.dll` (SDK 1.57: `SteamClient017`, `SteamUser021`) and reports "Steam unavailable" under the current runtime.
7. Mac Steam sets `STEAM_COMPAT_CLIENT_INSTALL_PATH` for compat tools (runtime spec §2); its value on macOS has not been recorded.
8. (Replaced by Amendment 2's facts.) **The runtime ships the bridge.** `Libraries/Wine/lib/wine/` contains `x86_64-unix/lsteamclient.so` (Mach-O x86_64, linked to `@rpath/ntdll.so` and libc++), `x86_64-windows/lsteamclient.dll` (PE32+) and `i386-windows/lsteamclient.dll` (PE32). winecx added them in four commits on 2026-08-25 ("add proton's steam client bridge", "build the bridge on macos", "link the unix half against libc++", "do not require the proton only client exports"). Its `unixlib.cpp` loads `steamclient.dylib` from `STEAM_COMPAT_CLIENT_INSTALL_PATH`, treats the two missing functions as optional (logging "host steamclient has no …, carrying on without it"), answers `Steam_IsKnownInterface` with 1 when absent, and skips `Steam_NotifyMissingInterface`.
9. In SMITE 2's prefix, `wineboot` placed `lsteamclient.dll` in `system32`, but there is no `C:\Program Files (x86)\Steam` and no `HKCU\Software\Valve\Steam` key. Nothing currently tells `steam_api64.dll` that Steam runs or where its client DLL is. That is the whole gap.
10. The maintainer's `loginusers.vdf` has one account and no `MostRecent` key.

## 3. Architecture

Amendment 2 replaces this diagram.

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

Only `run` and `waitforexitandrun` go through `steam.exe`, as in Proton. `runinprefix`, `getcompatpath` and `getnativepath` are unchanged. ~~The launcher passes the program as a Windows path (`Z:` plus the Unix path with backslashes); `Z:` is Wine's standard mapping of `/`.~~ Amendment 2: the launcher passes the game's macOS path as Steam gave it, and steam.exe converts it (Proton's `run_process`), to `S:` for a game in a Steam library.

## 4. Components

### 4.1 `lsteamclient`

~~Provided by the runtime (§2.8).~~ Amendment 2: built in-tree into `wine.app` (D-13). MacNeutron copies it into prefixes. If a future runtime drops it, the launcher logs `note: Steam bridge not installed` and starts games directly.

### 4.2 `steam.exe` (ours)

Replaced by Amendment 2's **steam.exe** block; kept for the record.

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

`bridge/probe.c`. `bridge/probe.sh <steam_api64.dll>` runs `steamprobe.exe <dll>` through `steam.exe` with `SteamAppId`
and `SteamGameId` 480 (Valve's public test app), in one of two modes. With a tool folder (`MACNEUTRON_TOOL_DIR`) it goes
through the launcher's `launch waitforexitandrun`, which prepares the prefix and copies the bridge in as for a game.
With a `wine.app` and a booted prefix (`MACNEUTRON_ARM64_APP`, `MACNEUTRON_ARM64_PREFIX`) it copies only `steam.exe` and
the bundle's lsteamclient (as `steamclient64.dll`) into the prefix's Steam folder and runs that `steam.exe` directly.
`wine-arm64/check.sh steam-bridge` runs both. The probe uses only flat C exports looked up with `GetProcAddress`, so it
needs no SDK headers. It:

1. calls `SteamAPI_ManualDispatch_Init`, then `SteamAPI_Init` (or `SteamAPI_InitFlat` on newer SDKs);
2. finds `SteamAPI_SteamUser_v0NN` and `SteamAPI_SteamFriends_v0NN` among the DLL's exports, trying the newest version first;
3. prints the SteamID and persona name;
4. calls `GetAuthSessionTicket` and pumps manual dispatch for up to 10 s, waiting for callback 163 (`GetAuthSessionTicketResponse_t`).

It exits 0 only when all four succeed.

## 5. Build: `make bridge`

Amendment 2 replaces the toolchain: the pinned llvm-mingw, with `steam.exe` and the test helpers built for arm64 into
`build/bridge/arm64/`. `make bridge-check` now runs on `build/wine-arm64/wine.app` (§9).

`make bridge` compiles `bridge/steam.c`, `bridge/probe.c` and `bridge/tests/helper.c` with Homebrew's `x86_64-w64-mingw32-gcc -O2 -static -s` into `build/bridge/`. If the compiler is missing, the error says `brew install mingw-w64`. `make bridge-check` runs `bridge/check.sh` against the installed runtime (§9).

## 6. Shipping and installation

- **App bundle.** `make app` depends on `make bridge` and copies `build/bridge/arm64/steam.exe` to `MacNeutron.app/Contents/Resources/steam.exe`. It can't sit next to the `macneutron` CLI in `Contents/Helpers`: codesign accepts only signed Mach-O code there.
- **Tool folder.** `RuntimeInstaller.writeToolFiles` installs the launcher and `steam.exe` into `<tool>/bin/`, taking `steam.exe` from next to the launcher it was given or, for the app's `Contents/Helpers/macneutron`, from `Contents/Resources`. Each file is written to a temporary name and renamed into place, and skipped when identical, so a game launching at that moment never sees a missing launcher. It runs on every runtime install and at every app start when a runtime is installed. That way an updated app updates the launcher and `steam.exe` without a reinstall.
- **Prefix.** Before `run` and `waitforexitandrun`, when the bridge is enabled, `PrefixManager.prepare` copies the files in Amendment 2's diagram (`steam.exe` into `drive_c/windows/system32/` and `drive_c/Program Files (x86)/Steam/`, `steamclient64.dll` into the latter), every launch, the same way it deploys graphics DLLs.

## 7. Launcher integration

- **When.** The bridge is enabled for `run` and `waitforexitandrun` when `ToolLayout.steamBridgeInstalled` (`bin/steam.exe`, the runtime's `aarch64-unix/lsteamclient.so` and `aarch64-windows/lsteamclient.dll` all exist) and `MACNEUTRON_NO_STEAM_BRIDGE` isn't `1`.
- **Command.** ~~`wine "C:\Program Files (x86)\Steam\steam.exe" <Windows path of target> <args…>`~~ Amendment 2: `wine C:\windows\system32\steam.exe <the game's macOS path> <args…>`; otherwise `wine <target> <args…>` as today.
- **Steam's client path.** `STEAM_COMPAT_CLIENT_INSTALL_PATH` keeps Steam's value when that folder holds `steamclient.dylib`, and is otherwise set to `SteamLocation.bundleMacOS` (no trailing slash). The launcher log records the folder used and what Steam passed (`note: Steam client folder <folder> (Steam passed <value or nothing>)`), and says so when that folder has no `steamclient.dylib`.
- **Account.** ~~`SteamLocation.activeAccountID()` reads `config/loginusers.vdf`. It takes the user with `MostRecent` `1`, otherwise the one with the largest `Timestamp`, and returns the low 32 bits of that user's SteamID64. The launcher sets `MACNEUTRON_STEAM_ACCOUNT` to it unless the variable is already set; when none is found, the launcher log says so.~~ Amendment 2: no account variable (R12).
- **Logging.** `MACNEUTRON_LOG=1` makes the default `WINEDEBUG` `warn+all,warn-seh,+loaddll,warn+steamclient`, and
  `MACNEUTRON_LOG=2` `warn+all,+loaddll,+steamclient`.

## 8. Errors

| Condition | Behavior |
|---|---|
| Runtime or tool folder lacks a bridge file | Game starts directly, with leftover client DLLs removed from the prefix (as for the escape hatch); launcher log: `note: Steam bridge not installed` |
| `MACNEUTRON_NO_STEAM_BRIDGE=1` | Game starts directly, and ~~`steam.exe`,~~ `steamclient64.dll` and `steamclient.dll` are removed from the prefix (Amendment 2: `steam.exe` stays, D-27): otherwise the game's `steam_api` loads the leftover client DLL (its registry values persist) and the bridge aborts the game for want of Steam's client path (seen in acceptance). Launcher log: `note: Steam bridge disabled by launch option` |
| No `steamclient.dylib` in the folder used | Launcher log: `note: steamclient.dylib not found in <folder>`; the game still starts |
| ~~No readable `loginusers.vdf` or no user in it~~ | ~~`ActiveUser` not written; launcher log: `note: no Steam account found in loginusers.vdf`~~ Amendment 2: no account rows |
| Launcher-style game (first process exits) | `steam.exe` keeps Steam "running" until no program is left in the prefix |
| Game crashes | ~~`steam.exe` still clears its pid~~ Amendment 2: `steam.exe` never clears its pid (R4); the exit code passes through |
| `steam.exe` killed | The stale pid is overwritten at the next launch |
| Copying bridge files into a prefix fails | Launch fails with `could not install the Steam bridge: <file>: <reason>`, like other prefix-preparation errors |

## 9. Testing

Rewritten for Amendment 2: what the tests check today.

- **Swift unit tests** (`Tests/MacNeutronCoreTests`):
  - `clientDirectory` keeps Steam's value when that folder holds `steamclient.dylib`, and otherwise falls back to the
    bundle's MacOS folder;
  - `uiLanguage` reads Steam's language from `registry.vdf`; it reaches steam.exe as `MACNEUTRON_STEAM_LANGUAGE`
    unless a launch option sets it;
  - `steamBridgeInstalled` needs `steam.exe` and both halves of lsteamclient;
  - `WINEDEBUG` gains the `steamclient` channel under `MACNEUTRON_LOG` (§7);
  - launcher: `waitforexitandrun` runs `C:\windows\system32\steam.exe` with the game's raw macOS path, Steam's client
    folder and no account variable, and the three games run the Steam folder's copy; `runinprefix` starts the game
    directly; so do the escape hatch and a missing bridge, each with its log note; the escape hatch takes both client
    DLLs out of the prefix and keeps both `steam.exe` copies, and a missing bridge takes `steamclient64.dll` out; the
    client folder is logged, and a missing `steamclient.dylib` noted; the game log hides `SteamUser`, `SteamAppUser`
    and secret-looking variables;
  - `PrefixManager` copies `steam.exe` into `system32` and the Steam folder and `steamclient64.dll` into the latter (no
    `steamclient.dll`), recopies only a changed file, and picks Valve's cached DLL or lsteamclient
    (`MACNEUTRON_STEAMCLIENT64` overrides); it links `s:` with the bridge or without and logs Proton's game-drive
    message;
  - `GameDrive`: the library's parent is the target; Proton's first substring match wins; an empty entry removes the
    drive; a legacy `SteamApps` folder passed as `…/steamapps` is the library; a library that fails Proton's checks
    gets its fallback; `PROTON_SET_GAME_DRIVE` `0` or empty turns the drive off; a non-link at `s:` is left alone;
  - `writeToolFiles` installs `steam.exe` from next to the launcher, from the app's `Contents/Resources` or from
    `--steam-exe`, and replaces only a changed launcher.
- **`bridge/check.sh`** (`make bridge-check`, after `probe.sh`'s redaction self-test) runs `steam.exe` on `wine.app`
  without Steam: through a tool folder it assembles from `build/wine-arm64/wine.app` by default, a given tool folder, or
  an arm64 prefix. `SteamGameId` is set, the work folder's name has a space and a non-ASCII letter, and a stand-in
  `open(1)` records what reaches Mac Steam. Its rows:
  - starting the program: the exit code passes through; arguments with spaces, quotes and an empty string arrive
    unchanged; a missing program exits 1; a launcher-style child, one started with `CREATE_BREAKAWAY_FROM_JOB`
    included, still sees Steam; `PROTON_HIDE_PROCESS_WINDOW` (any value not starting with 0) starts it hidden; Source
    SDK's `-game` path is converted, unquoted or quoted (with a space and a non-ASCII letter), and a quoted one ending
    in a separator keeps it, with the next argument kept apart;
  - what a game sees: the registry names a live Steam and `steamclient64.dll`; `SteamPath` and `ValvePlatformMutex`
    (the steam.exe that ran); Steam's named events and hidden window; the SteamStub start handshake is answered; the
    `Apps` key and Steam's language; an earlier build's `ActiveUser` is removed; without `SteamGameId`, no pid, named
    events or `ValvePlatformMutex`, and no `libraryfolders.vdf`;
  - a `link2ea://` target clears EA's `IsUnavailable` and exits 0;
  - commands: `HKCR\steam` is Proton's handler; a game's `steam://` link and `steam.exe steam://…` reach Mac Steam;
    `-applaunch <id>` reaches it as `rungameid/<id>`, and with arguments as `run/<id>//<args>/`; arguments with a
    non-ASCII letter, an empty one, `/`, `%`, a tab, an embedded quote and a trailing backslash give exactly the
    Windows-quoted, UTF-8 percent-encoded URL; a 2000-character `steam://` link and one with Japanese letters reach it
    whole; a game restarting itself through `steam://rungameid/<its id>` restarts in place, and Mac Steam isn't asked;
    a steam.exe that a game's link starts through `HKCR\steam` (no Windows standard handles) still logs its failures:
    a restart link with no game steam.exe to restart puts `Restart event not found` on stderr;
  - through the launcher (tool-folder mode): the exit code, the arguments, a missing program, a launcher-style child
    (with `CREATE_BREAKAWAY_FROM_JOB` too), a live Steam in the registry and system32's `ValvePlatformMutex` again, and:
    with the bridge off a game's `steam://` link still reaches Mac Steam; a game in a library runs from `S:` (a library
    path with a space and a non-ASCII letter, and one without a space); a library that fails Proton's checks gets its
    fallback; a console program gets a console of its own and its exit code reaches Steam; a `.bat` target exits 3 and
    steam.exe still exits 0 (ShellExecute: CreateProcess would pass the 3 on); `libraryfolders.vdf` names the libraries
    as the game sees them, and Steam's config folder exists;
  - the pid (R4): written as 1 first, it is left as a dead pid other than 0 and 1 by a steam.exe that fails to start
    its program, and again by one whose game ended; the next steam.exe registers again;
  - a no-hang row: steam.exe run under a terminal (`script(1)`) ends with the program's exit code. steam.exe is a GUI
    program, so it has no console host to leave.
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
| Does `SteamAPI_Init` need `ActiveUser`? (probe with and without `MACNEUTRON_STEAM_ACCOUNT`) | ~~Either way, keep writing it when known; record the answer~~ Amendment 2: Proton writes no `ActiveUser`, and steam.exe deletes one an earlier build wrote (R12) |
| Does Wine load the prefix copy `steamclient64.dll` and find `lsteamclient.so`? | Yes → §6 as written. No → point `SteamClientDll64` at `C:\windows\system32\lsteamclient.dll` (placed there by `wineboot`), drop the DLL copies, record the change |

## 11. Risks

- **The runtime drops or breaks the bridge.** (Since 0.1.0 the runtime is our `wine.app`, installed from
  MacNeutron.app; there is no `RuntimePin`.) The launcher detects missing files and falls back to a direct start. A
  broken bridge shows up in `+steamclient` logs. ~~The pinned runtime changes only when MacNeutron updates
  `RuntimePin`, and a pin update must re-run acceptance item 1.~~ A change to lsteamclient's pin (`LSTEAMCLIENT_COMMIT`
  in `wine-arm64/deps.pins`) or patches, or to Wine patch 0036, must re-run acceptance item 1.
- **License.** ~~`lsteamclient` stays the runtime distributor's (Steamworks SDK license); MacNeutron doesn't redistribute it. The repo has no `LICENSE` file yet; our own code (`steam.c`, `probe.c`, tests) needs one before public release. That is the maintainer's choice and not part of this plan.~~ Since Amendment 2, `wine.app` ships `lsteamclient` under Valve's Steamworks SDK licence (`licenses/lsteamclient/`), and the repo's own code is MIT (`LICENSE`), with the exceptions the README names.
- **Intel slice.** (Superseded: since 0.1.0 `lsteamclient.so` is arm64, so it loads the arm64 slice.) If Valve drops the x86_64 slice of `steamclient.dylib`, the bridge stops working; the launcher's missing-file note won't catch that (the file exists), but `+steamclient` logs `unable to load native steamclient library`. There's no mitigation short of an arm64 Wine.
- **SDK versions newer than the runtime's `lsteamclient`.** Games built against a newer Steamworks SDK may fail to get some interfaces until the runtime updates.
- **Rosetta after macOS 27.** (Superseded: the Rosetta runtime was removed in 0.1.0.) Apple has said that beyond macOS 27 it keeps only a subset of Rosetta aimed at games. The whole runtime depends on that; the bridge adds no new dependency.
