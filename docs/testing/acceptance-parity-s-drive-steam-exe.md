# Acceptance: the S: game drive and the full steam.exe port (Proton parity P0-3 + P1-1)

Spec: `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md` (approved 2026-10-12). Plan:
`docs/superpowers/plans/2026-10-12-macneutron-parity-s-drive-steam-exe.md` (approved 2026-10-12), with rulings R1-R20.
Research: `docs/research/2026-10-12-proton-parity-p03-p11/`. Deviations from Proton, each with its reason: the Steam
bridge spec's Amendment 2 (D-1 to D-33).

## What changed

1. **S: game drive** (4c06871). `PrefixManager.prepare` links `pfx/dosdevices/s:` to the game's library on every
   preparation, as Proton's `setup_dir_drive` does:
   - the library is the first `STEAM_COMPAT_LIBRARY_PATHS` entry contained in `STEAM_COMPAT_INSTALL_PATH`;
   - the target is the library's parent when Proton's checks pass, and the steamapps folder itself when they don't;
   - Proton's message goes to launcher.log;
   - `PROTON_SET_GAME_DRIVE=0` or empty removes the link.
2. **Proton's `run_process`** (347d6c6):
   - a macOS path becomes its DOS path, which is `S:` in a library;
   - console programs get their own console;
   - targets that aren't an `.exe` go through ShellExecute, with the EA workarounds;
   - Source SDK's `-game` path is converted;
   - the launcher passes the game's raw path;
   - `steam.exe` is built as a GUI program.

   One measured deviation (D-22): the game inherits handles, so its own output stays in MacNeutron's game log.
3. **Proton's `main` under the `SteamGameId` gate** (48d9aef):
   - the hidden "Steam" window;
   - `ValvePlatformMutex` from `GetModuleFileNameW`;
   - no `ActiveUser`, and the launcher no longer reads `loginusers.vdf`.
4. **`C:\windows\system32\steam.exe`** (5d0da56): the three Steam-folder games are handled, and turning the bridge off
   keeps `steam.exe` for `steam://` links.
5. **The command handler and in-place restart** (165350b):
   - `HKCR\steam` is Proton's value;
   - `steam://` links and `-applaunch` reach Mac Steam through winebrowser;
   - a game restarting itself restarts in place.
6. **`libraryfolders.vdf` from `steam.exe`** (5e46d56): the library behind `S:` is listed as `S:\`. The library paths
   are read as UTF-8. Proton's `getenv` mangled a non-ASCII path and stalled Wine's lookup; the check hung there twice
   before the fix.
7. **Final review fixes**:
   - `GameDrive` checks the `steamapps` name as Steam spells the library entry (the entry's own last component, or the
     resolved name when the entry is itself a symlink). Proton's Python `realpath` keeps the spelling it was given for
     a part that isn't a link, but Darwin's `realpath(3)` returns the on-disk case, so a legacy on-disk `SteamApps`
     folder that Mac Steam passes as `.../steamapps` failed the check, and every game in that library got Proton's
     fallback (`S:\common\<Game>`). This is parity with Proton, not a deviation; a test pins it.
   - `steam.exe -applaunch <id> <arguments>` forwards `steam://run/<id>//<arguments>/` to Mac Steam, not
     `steam://rungameid/<id>` with the arguments dropped:
     - each following argument is used, quoted when it holds a space or tab or is empty, joined by spaces and
       percent-encoded as UTF-8 (`A-Z a-z 0-9` and `- _ . ~ + : , = @ ! $ ' ( ) * ; &` stay literal);
     - `-applaunch <id>` alone still forwards `steam://rungameid/<id>`, and options before `-applaunch` are still
       logged and dropped;
     - Proton passes the arguments to the Linux client's command line, which Mac Steam doesn't have, so the URL carries
       them. Valve's wiki documents `steam://run/<id>//<args>/`; the encoding follows common usage, because `open(1)`
       rejects a URL with a space.
   - Source SDK's `-game` fixup also converts a quoted path, keeping the quotes (Wine quotes an argument holding a
     space, and every path under `~/Library/Application Support/Steam` has one). It converts as UTF-8
     (`WideCharToMultiByte` with `CP_UNIXCP`, as `run_process` does), not by narrowing each `WCHAR` to a byte, which
     gave the Windows-1252 bytes that stalled Wine's lookup (D-32). It stays behind Proton's `\Source SDK Base` gate
     and converts the first `-game` only. Proton converts an unquoted path only, so this is a hardening with a D-line.
   - `forward_to_mac_steam` takes the `steam://` URL from the wide command line (`CommandLineToArgvW`), not the C
     runtime's ANSI `argv`, builds its winebrowser command on the heap, and logs every failure, so a URL of any length
     and with letters outside the Windows code page reaches Mac Steam whole (part of D-12).
   - `bridge/check.sh` rows:
     - Source SDK's quoted `-game` path with a space and a non-ASCII letter is converted;
     - `-applaunch` with arguments becomes `run/<id>//<args>/`;
     - a 2000-character `steam://` link and one with Japanese letters reach Mac Steam whole;
     - the `.bat` target exits 3 and `steam.exe` must still exit 0, which pins ShellExecute (CreateProcess would pass
       the 3 on);
     - the failed `steam.exe` row writes pid 1 first, then requires a nonzero pid other than 1 that is dead, which pins
       Proton's rule that `steam.exe` never clears its pid (R4);
     - "steam.exe ends under a terminal" is described as a generic no-hang row: `steam.exe` is a GUI program, so
       `FreeConsole` has no console host to leave.
   - Documentation:
     - the README says which names a game log hides (`SteamUser`, `SteamAppUser` and secret-looking variable names; no
       account ID is in the environment), and that `MACNEUTRON_NO_STEAM_BRIDGE=1` removes the Steam API connection but
       leaves `steam://` links working;
     - the `swift test` row in Gates counts the deleted tests;
     - the `addSteamClient` comment no longer says the launcher tells `steam.exe` who is logged in;
     - the Steam bridge spec and its Amendment 2 are corrected where the review found them out of step with the code:
       sections that still listed the deleted tests or the `ActiveUser` write, D-26's registry differences from Valve's
       `wine.inf`, D-33's stand-in for `steamclient_init_registry`, the D-entries that lost detail, the missing
       `steam.exe=b` override and D-28's wording.

## Gates (2026-10-12)

| Gate | Result |
|---|---|
| `swift test` | 324 tests passed (329 after Task 1; then 7 tests deleted with their code, `windowsPath`, `libraryFolders`, three `activeAccountID`, `accountFromLaunchOptionsWins` and `steamFilesComeWithTheBridge`, and 2 added, net -5) |
| `make bridge-check` | all passed, 47 `ok`; the restart row passed 3 runs in a row |
| `make smoke` | all passed, including console programs started through the launcher |
| `sh wine-arm64/check.sh steam-bridge` | `PASS steam-bridge`: the probe exits 0 directly and through the launcher (GUI `steamprobe.exe`, Mac Steam running) |

Each task's RED was recorded first; the ledger has the details. Corrections made while running:
- the `con.txt` test file is a reserved Windows device name;
- `check.sh` assignments made before a function call persisted to later rows;
- a guessed `/private/var` path was wrong.

## Maintainer rows (build/dev/MacNeutron.app)

Part A item 2: P0-3 stays P0 only if rows 1-3 pass; otherwise it is recorded as P1.

1. **Palworld:**
   - it starts and loads a world;
   - its game log, with `/usr/bin/env MACNEUTRON_LOG=1 %command%`, shows `S:\steamapps\common\Palworld\…`;
   - SteamStub passes. — *pending*
2. **SMITE 2:** one match. — *pending*
3. **A launcher-based game whose prefix last ran under the `Z:` build** (named by the maintainer): it starts through its
   launcher; record whether the launcher asks to repair or relocate. — *pending*
4. **Timberborn** (passthrough): unchanged. — *pending*
5. **Store link:** an in-game store link opens Mac Steam. — *pending*
6. **No stray windows:** no Dock icon for steam.exe, and no console window for the three games. — *pending*
7. **Opt-out:** `/usr/bin/env PROTON_SET_GAME_DRIVE=0 %command%` on one game shows `Z:` again (its game log). —
   *pending*
