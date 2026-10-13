# Acceptance: the S: game drive and the full steam.exe port (Proton parity P0-3 + P1-1)

Spec: `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md` (approved 2026-10-12). Plan:
`docs/superpowers/plans/2026-10-12-macneutron-parity-s-drive-steam-exe.md` (approved 2026-10-12), with rulings R1-R20.
Research: `docs/research/2026-10-12-proton-parity-p03-p11/`. Deviations from Proton, each with its reason: the Steam
bridge spec's Amendment 2 (D-1 to D-34).

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
7. **Final review fixes** (859e09b, e11fd38 and the commits after them):
   - `GameDrive` checks the `steamapps` name as Python's `realpath` gives it: the entry as Steam spells it, a link's
     text hop by hop, `.` dropped and `..` taken back. Darwin's `realpath(3)` returns the on-disk case, so a legacy on-disk `SteamApps` folder that Mac
     Steam passes as `.../steamapps` failed the check, and every game in that library got Proton's fallback
     (`S:\common\<Game>`). Parity with Proton; Swift tests pin it.
   - `steam.exe -applaunch <id> <arguments>` forwards `steam://run/<id>//<arguments>/`, not `steam://rungameid/<id>`
     with the arguments dropped: each argument spelled as a Windows command line spells it, percent-encoded as UTF-8
     (D-12). `-applaunch <id>` alone still forwards `steam://rungameid/<id>`.
   - Source SDK's `-game` fixup also converts a quoted path (every path under `~/Library/Application Support/Steam`
     has a space), as UTF-8, and trailing separators stay readable before the closing quote (D-34).
   - `steam://` links come from the wide command line: any length, any letter (D-12).
   - `steam.exe` logs through Wine's debug output, as Proton's `WINE_ERR`. A `steam.exe` that a game's link starts has
     no Windows standard handles, so its failure lines were lost. Lines are UTF-8 and cut to 1000 bytes: Wine's debug
     line holds 1020 and raises past them.
   - `bridge/check.sh` rows pin the `steam.exe` changes, Swift tests the `GameDrive` one. The pid rows pin R4 on a
     failed start and on a normal end, the `.bat` row pins ShellExecute, and the terminal row is a no-hang row. The rows
     that couldn't fail before were seen failing under a temporary mutation (the ledger names each).
   - Documentation: the README (what game logs hide, what the escape hatch keeps), the bridge spec (Amendment 2 names
     every section the code contradicts, §9 lists what the tests check, the D-lines restored from the parity spec's
     Part 4 and the new ones) and the `swift test` count below.

## Gates (2026-10-12)

| Gate | Result |
|---|---|
| `swift test` | 324 tests passed (329 after Task 1; then 7 tests deleted with their code, `windowsPath`, `libraryFolders`, three `activeAccountID`, `accountFromLaunchOptionsWins` and `steamFilesComeWithTheBridge`, and 2 added, net -5). After the final review fixes: 326 passed (two `GameDrive` tests added) |
| `make bridge-check` | all passed, 47 `ok`; the restart row passed 3 runs in a row. After the final review fixes: all passed, 57 `ok` |
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
8. **`-applaunch` arguments** (optional; D-12 leaves it to you): with Mac Steam running and the game not running, in
   Terminal:

   ```sh
   env STEAM_COMPAT_DATA_PATH="<library>/steamapps/compatdata/<appid>" \
     ~/Library/Application\ Support/MacNeutron/compatibilitytools.d/macneutron/bin/macneutron \
     launch runinprefix 'C:\windows\system32\steam.exe' -applaunch <appid> -windowed
   ```

   Record whether Mac Steam starts the game with `-windowed`, and whether it asks you to confirm the arguments. —
   *pending*
