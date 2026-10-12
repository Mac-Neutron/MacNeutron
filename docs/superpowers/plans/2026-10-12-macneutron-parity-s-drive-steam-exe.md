# Proton parity: the S: game drive and the full steam.exe port (P0-3 + P1-1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Games see their install folder as `S:\steamapps\common\<Game>` and run through a `steam.exe` that does what
Proton 11's does: Unix-path conversion, ShellExecute targets, the command handler and in-place restart, the
`SteamGameId` gate, the hidden Steam window and `libraryfolders.vdf`.

**Architecture:**
- **The drive.** `PrefixManager.prepare` links `dosdevices/s:` to the game library's parent, as Proton's
  `setup_dir_drive` does.
- **The launch line.** The launcher passes the game's raw Unix path to `C:\windows\system32\steam.exe`.
- **steam.exe.** `bridge/steam.c` is ported closely from Proton's `steam_helper/steam.c` (proton_11.0, BSD-3, notice
  already carried) and built as a GUI program, as Proton's is. Its `main` follows Proton's order, and Wine converts the
  path to the deepest drive, `S:`.
- **macOS deviations.** Each is a D-line in the Steam bridge spec, chiefly `steam://` forwarding through winebrowser to
  `open(1)`.

**Tech Stack:** C for arm64 llvm-mingw PEs (`bridge/steam.c`, `bridge/tests/helper.c`), plus the x64 `bridge/probe.c`.
Swift 6 (MacNeutronCore, Swift Testing). POSIX sh (`bridge/check.sh`).

**Spec:** `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md`, approved 2026-10-12. The plan uses:
- Part A, items 2 and 3;
- Part B's P0-3 and P1-1;
- Part C's M1, M2, M3, L2, L5, L6 and L7.

Part A binds where it differs. Rulings R2 and R10 below override Part B's own text, and Task 7 amends the spec for
them.

**Research (exact behaviour, with line citations):** `docs/research/2026-10-12-proton-parity-p03-p11/`:
- `proton-steam-c.md`: Proton's `steam.c` at `proton_11.0` (bc896217);
- `proton-script.md`: the `proton` script;
- `macneutron-side.md`: our code, and which existing test each change breaks.

A step that says "as Proton" means the behaviour those files give, line by line.

## Global Constraints

- **The maintainer's rule:** the Steam connection does what Proton 11 does. Any difference is for a macOS reason, or is
  one of the rulings below, and each is written as a D-line in Task 7.
- **Valve's code:** port `steam.c` closely and keep Valve's BSD-3 notice (`bridge/steam.c:13-43`,
  `bridge/Proton-steam-LICENSE.txt`). Never commit Proton's source files themselves.
- **Build:** `steam.exe` is an arm64 llvm-mingw PE built `-mwindows` (`Makefile:30`).
  - `wine_get_dos_file_name` (kernel32) and `__wine_unix_spawnvp` (ntdll) come through `GetProcAddress`.
  - `#define CP_UNIXCP 65010`.
  - Results from `wine_get_dos_file_name` are freed with `HeapFree(GetProcessHeap(), 0, p)`.
- **Tests:**
  - `bridge/check.sh` runs without Steam and must never open the real Mac Steam. `steam://` reaches the stub `open`
    only through winebrowser's `Browsers` key (`bridge/check.sh:52-61`).
  - Every row that reads the stub's `open.txt` uses the `opened()` poll (Task 5).
  - Every row that reads a helper's output or file passes it through `tr -d '\r'`.
  - Never run Steam or a game. Never read `~/Library/Application Support/MacNeutron` or `steamapps/compatdata` except
    through the tests' own temporary folders.
- **Privacy:** no file the tests write holds a SteamID or an account name.
- **Gates:**
  - Per task: the task's own command.
  - At the end:
    - `swift test`;
    - `make bridge-check`;
    - `make smoke`;
    - `sh wine-arm64/check.sh steam-bridge` (needs Steam running and SMITE 2 installed; skip and say so if Steam isn't
      running);
    - the maintainer's smoke (Task 7).
  - Use `/usr/bin/grep` in gates.
- **Commits:** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Never push.

## Rulings this plan makes

The maintainer may override any of these at plan review. Each becomes a D-line in Task 7 when it differs from Proton.

| # | Question | Ruling | Why |
|---|---|---|---|
| R1 | M1: validation fails | Proton's fallback: `s:` → the `steamapps` folder itself, and the game sees `S:\common\<Game>`. Proton's message goes to launcher.log | Parity. Part A lets the maintainer choose, and the rule picks Proton |
| R2 | Library pick | Proton's test exactly: the first `:`-split entry, empty pieces included, for which `entry.isEmpty \|\| install.contains(entry)`, with no retry. An empty matched entry gives an empty target, which removes `s:` | Spec Part B asked for a path-component match, which has no macOS reason |
| R3 | Subsystems | `steam.exe` is `-mwindows`, as Proton's. `CREATE_NEW_CONSOLE` for a console image reached by a Unix path. The test programs `helper.exe` and `steamprobe.exe` are GUI programs, so they keep the inherited pipe; a console copy, `helper-console.exe`, covers the console rows and writes to a file argument | Parity; the tests change, not the port |
| R4 | Order in `main` | Proton's: `pid` is registered before the child, `bInheritHandles=FALSE`, no `CREATE_SUSPENDED`, and no `unregister_steam`. The last `steam.exe` owns `pid`, and a failed one leaves its own dead `pid` (Proton behaviour) | Parity; there is no macOS reason for ours |
| R5 | DIPC semaphores | Keep ours: `STEAM_DIPC_CONSUME` and `SREAM_DIPC_PRODUCE` are created **before** `run_process`. Keep our ack-event lookup in `\Sessions\1\BaseNamedObjects`, each round | Stricter than Proton, because SteamStub can open them as soon as it starts. The lookup is the directory our Wine uses, verified with Palworld. D-line |
| R6 | Restart sender | `OpenEventW(SYNCHRONIZE \| EVENT_MODIFY_STATE, …)` | Our Wine refuses `SetEvent` otherwise (`W/dlls/ntdll/unix/sync.c:792-796`); Valve's own precedent is db9e6ffb. D-line |
| R7 | steam.c:1028 | The intended semantics: `ret = WaitForMultipleObjects(...)`, and loop only on `WAIT_OBJECT_0 + 1`, with a comment | A precedence bug that differs only on a failed wait |
| R8 | Forwarding (D-12) | `steam://` URLs go to Mac Steam through `CreateProcessW("C:\windows\system32\winebrowser.exe \"<url>\"")`, which then calls `open(1)`. Strip a leading `--`; map `-applaunch <id>` to `steam://rungameid/<id>`; log and drop other `-` options; map no arguments to `steam://open/main` | macOS: there is no `steam-runtime-steam-remote` or `steam` binary. winebrowser keeps check.sh's stub reachable |
| R9 | `HKCR\steam` | Proton's value, `"C:\Program Files (x86)\Steam\steam.exe" -- "%1"`, written by `steam.exe` under the gate, plus our `URL Protocol` value. A prefix that has never run with the bridge has no handler | Our prefix isn't built from Valve's `wine.inf`. D-line |
| R10 | Bridge removed | `removeSteamBridge` removes only the client DLLs; both `steam.exe` copies stay, so `steam://` links keep working | Fixes the critic's escape-hatch regression. Amends Part B P1-1 step 1 and the A4 removal note |
| R11 | L7 | `ValvePlatformMutex` comes from `GetModuleFileNameW`, with `\` turned into `/`, as Proton | Parity. Removes D-13a |
| R12 | L2 | Drop `ActiveUser`, the launcher's `loginusers.vdf` read and `MACNEUTRON_STEAM_ACCOUNT`. Under the gate, `steam.exe` also deletes `ActiveProcess\ActiveUser`, which earlier builds wrote | Part A: Proton never writes it, and it is account data |
| R13 | L5, P1-2 | No GamingRepair (it waits for P1-4) and no `steamclient_init_registry` (P1-2) in this plan | Part A |
| R14 | EA desktop timeout | Port the call. Our Wine rejects index 1000 and logs an ERR | Not ported yet: it needs a Valve-Wine patch. D-line |
| R15 | Non-symlink at `s:` | Leave it in place and log a note | Never delete or block on a player's own file. Proton's abort is an unhandled exception, not a design. Minor D-line |
| R16 | Three appids | 311210, 1985810 and 1549250 (by `SteamGameId`) run `C:\Program Files (x86)\Steam\steam.exe` | M3. Only the location, until WoW64 (D-14) |
| R17 | `t:` steamdrive, `STEAM_COMPAT_CONFIG` | Not ported | Opt-in in Proton, and absent from both games' logs. D-line "not ported yet" |
| R18 | `PROTON_WAIT_ATTACH` | Port it faithfully; on Mac only a Windows debugger ends the wait | Our Wine has no class 1100. D-line |
| R19 | EA `SE_ERR_NOASSOC` recovery | Skip the `MsiConfigureProductExW REMOVE=ALL` uninstall and the `RegDeleteTreeW` until P1-4. Port the rest of the EA block | macOS: install scripts never run here (D-16), so nothing would reinstall EA afterwards. D-line |
| R20 | `setup_vr_registry` | Port it: `LoadLibraryW(L"vrclient_x64")` fails and logs an ERR | Parity; harmless |

## Review Focus

1. **A library whose path has no space** (an external drive). steam.exe takes the unquoted branch, and the game still
   gets `S:\…`, with its arguments unchanged. Test: Task 2, the two rows on a library under `mktemp -d` with no space.
2. **A library on another volume, or one whose parent is read-only.** Proton's fallback applies: `s:` is the steamapps
   folder, and the game starts from `S:\common\…`. Tests: Task 1, `aLibraryThatFailsValidationGetsProtonsFallback`;
   Task 2, the `SteamApps` row.
3. **A game that restarts itself through `steam://rungameid/<its id>`.** It restarts in place, and Mac Steam is not
   asked to launch it. Test: Task 5, the restart row (start count 2, no `open.txt`).
4. **A player who turns the bridge off with `MACNEUTRON_NO_STEAM_BRIDGE=1`.** The game's `steam://` store links still
   open Mac Steam. Test: Task 4, the bridge-off `steam://` row.
5. **A console-subsystem launch target.** It gets its own console, still starts, and its exit code still reaches Steam.
   Test: Task 2, the console and exit-code rows. These are regression rows: a GUI parent gives any console child a
   console.

---

### Task 1: The S: game drive in `PrefixManager` (P0-3 step 1)

**Files:**
- Create: `Sources/MacNeutronCore/GameDrive.swift`.
- Modify:
  - `Sources/MacNeutronCore/PrefixManager.swift`: in `prepare`, after `ProfileLinks.apply` (`:95-96`) and outside
    `if steamBridge`.
  - `Sources/MacNeutronCore/RuntimeInstaller.swift`: `realPath` (`:149-154`) becomes internal and shared.
  - `Sources/MacNeutronCore/ProfileLinks.swift`: `resolved` (`:145-150`) uses `realPath`, and `replaceLink`
    (`:152-161`) becomes internal.
- Test: `Tests/MacNeutronCoreTests/GameDriveTests.swift`, `Tests/MacNeutronCoreTests/PrefixManagerTests.swift`.

**Interfaces:**
- Produces:
  ```swift
  enum GameDrive {
      /// Proton's try_get_game_library_dir + get_validated_steamapps_parent (proton:299-322): the link target ("" or nil
      /// removes the link). A failed validation returns the raw entry (Proton's fallback) and appends Proton's message.
      static func target(environment: [String: String], device: (String) -> dev_t? = GameDrive.device,
                         notes: inout [String]) -> String?
      /// stat(2)'s st_dev. Tests pass their own `device` (a parameter, not a global: tests run in parallel).
      static func device(_ path: String) -> dev_t?
      /// Proton's setup_dir_drive for "s:" (proton:330-345) with PROTON_SET_GAME_DRIVE (proton:57-58, 1630-1637).
      static func apply(prefix: URL, environment: [String: String]) -> [String]   // notes for launcher.log
  }
  func realPath(_ path: String) -> String?   // realpath(3)
  ```

**Behaviour (proton-script.md §1-§4, with R2 and R15):**
- **`target`:**
  1. Both variables must be present; otherwise return nil.
  2. Pick the entry: `split(separator: ":", omittingEmptySubsequences: false)`, then the first entry where
     `entry.isEmpty || install.contains(entry)`. If none matches, return nil.
  3. Validate it:
     - `realPath(entry)` is a folder named exactly `steamapps`;
     - its parent is writable (`access(W_OK)`);
     - `device(path) == device(parent)`.
  4. Pass: return the resolved parent. Fail: return the raw entry, and append the note
     `Proton: Error: unable to use parent for game drive, path <entry>`.
- **`apply`:**
  - On unless `PROTON_SET_GAME_DRIVE` is `"0"` or `""`.
  - `s:` is `pfx/dosdevices/s:`; create `dosdevices` if it is missing.
  - On with a non-empty target:
    - create the link;
    - if `readlink` differs as a string, replace it atomically (`replaceLink`).
  - On with nil or `""`, or off: remove the link if it exists.
  - A non-symlink at `s:` is left alone, with the note `game drive: dosdevices/s: is not a link, left alone`.
  - Use `symlink(2)` and `readlink(2)`, never the process runner.

- [ ] **Step 1: Write the failing tests** (GameDriveTests).
  - Use a temporary library `<tmp>/Lib Ünï/steamapps/common/Game`.
  - Compare against `realPath` results: `/var` resolves to `/private/var`.

  Rows:
  - `theLibrarysParentIsTheTarget`: `"<other>:<lib>/steamapps"` → `realPath(<lib>)`.
  - `theFirstSubstringMatchWins`: `"<tmp>/L:<tmp>/L2/steamapps"`, with install under `<tmp>/L2/steamapps/…`, → the raw
    `<tmp>/L`. It matches first as a substring, then fails validation, so the fallback applies.
  - `anEmptyEntryMatchesAndRemovesTheDrive`: `"::<lib>/steamapps"` → `""`, with one note whose path is empty. `apply`
    then removes an existing `s:`.
  - `aLibraryThatFailsValidationGetsProtonsFallback`: each case returns the raw entry and one note:
    - basename `SteamApps`;
    - a read-only parent (`chmod 0555`, restored);
    - a `device` argument that answers differently for the library and its parent.
  - `noMatchOrMissingVariablesMeansNoDrive`: returns nil.
  - `applyLinksRetargetsAndRemoves`:
    - link made;
    - target changed → retargeted;
    - `PROTON_SET_GAME_DRIVE=0` → removed, and `PROTON_SET_GAME_DRIVE=` → removed;
    - `PROTON_SET_GAME_DRIVE=false` → linked;
    - no match → removed.
  - `aNonLinkAtSIsLeftAlone`: a regular file stays, with one note.

  PrefixManagerTests:
  - `prepareLinksTheGameDriveWithOrWithoutTheBridge`:
    - `prepare(environment:)` with both variables links `s:`;
    - `steamBridge: true` links it too;
    - `winebootCount` is still 1.
  - `prepareLogsTheGameDriveNotes`: a failed validation writes `note: Proton: Error: unable to use parent for game
    drive, path …` to launcher.log.
- [ ] **Step 2: Run them to see them fail.** `swift test --filter 'GameDriveTests|PrefixManagerTests'`. Expected: a
  compile failure (no `GameDrive`).
- [ ] **Step 3: Implement** `GameDrive`, the shared helpers, and the `prepare` call, which appends each note as `note:
  <text>`.
- [ ] **Step 4: Run them to see them pass.** `swift test` → all pass.
- [ ] **Step 5: Commit.** `prefix: the S: game drive, as Proton's setup_dir_drive (PROTON_SET_GAME_DRIVE=0 turns it off)`.

### Task 2: steam.exe's `run_process`, the raw Unix target, and GUI test programs (P0-3 steps 2-3, P1-1 step 2, R3-R5, R19)

**Files:**
- Modify:
  - `bridge/steam.c`.
  - `Makefile:29-31`:
    - steam.exe gets `-mwindows -luser32 -lshell32 -lshlwapi -lole32 -lmsi`;
    - steamprobe.exe and helper.exe get `-mwindows`;
    - a new `$(BRIDGE)/arm64/tests/helper-console.exe` is built from the same `helper.c` without `-mwindows`.
  - `Sources/MacNeutronCore/Launcher.swift:155`: pass `request.target` raw.
  - `bridge/tests/helper.c`: modes `module`, `console <file>` and `marker <file>`.
  - `bridge/check.sh`.
  - `Tests/MacNeutronCoreTests/LauncherTests.swift:240`.
- Test: `bridge/check.sh`, LauncherTests.

**Interfaces:**
- `static HANDLE run_process(BOOL *should_await, BOOL game_process)`, Proton's `steam.c:417-649`:
  - the argv[0] skip with `find_quote`;
  - the Unix branch, with `SHGetFileInfoW(SHGFI_EXETYPE)` → `CREATE_NEW_CONSOLE`;
  - `SetConsoleCtrlHandler(console_ctrl_handler, TRUE)`;
  - `should_use_shell_execute` and the ShellExecute loop;
  - the EA block with R14 and R19, plus `try_recover_eadesktop_symlink`;
  - `fixup_cmdline_paths`;
  - `CreateProcessW(…, FALSE, CREATE_UNICODE_ENVIRONMENT | console flag, …)`.

  It returns the child, or `INVALID_HANDLE_VALUE` for ShellExecute or a failure. `*should_await` is
  `!use_shell_execute`. Task 5 calls it again to restart. Hardening, each with a one-line comment:
  - `if (!dos)`, run the line unchanged;
  - `main` initialises `should_await = TRUE`.
- `main` this task:
  - today's steps, but with R4: register (`pid` and today's keys, including the winebrowser `HKCR`) before
    `run_process`; no suspend; no `unregister_steam`;
  - the DIPC semaphores before `run_process` (R5);
  - a `run_process` failure returns 1.
- New helper modes (wide paths through `_wfopen`):
  - `module` prints `GetModuleFileNameW(NULL)`;
  - `console <file>` writes `own_console=1` to `<file>` when `GetConsoleProcessList` counts 1;
  - `marker <file>` writes `marker` to `<file>`.

- [ ] **Step 1: Write the failing rows.**
  - **Set-up**, in check.sh's tool-folder block (`if [ -n "$COMPAT" ]`, `:114`):
    - `SLIB="$WORK/Lib Ünï"`, with `hélper.exe` and `helper-console.exe` copied to `$SLIB/steamapps/common/Game/`;
    - `NS=$(mktemp -d "${TMPDIR:-/tmp}/mnlib.XXXXXX")`, the same copies under `$NS/steamapps/common/Game/`, removed at
      exit. Skip its rows with an `info` line if `$NS` contains a space;
    - `lib() { L waitforexitandrun "$1/steamapps/common/Game/$2" "${@:3}"; }`, with `STEAM_COMPAT_LIBRARY_PATHS="$1/steamapps"`
      and `STEAM_COMPAT_INSTALL_PATH="$1/steamapps/common/Game"` added to `L()`'s environment;
    - `lib_status() { lib "$@" > /dev/null 2>&1 && echo 0 || echo $?; }`.

    Write `lib` with positional handling valid in POSIX sh; `"${@:3}"` stands for "the remaining arguments".
  - **Rows:**
    ```sh
    expect "a game in a library runs from S:" "$(lib "$SLIB" hélper.exe module | tr -d '\r')" 'S:\steamapps\common\Game\hélper.exe'
    expect "on a library without spaces too (unquoted branch)" "$(lib "$NS" hélper.exe module | tr -d '\r')" 'S:\steamapps\common\Game\hélper.exe'
    expect "and its arguments arrive unchanged" "$(lib "$NS" hélper.exe args 'a b' x | tr -d '\r')" '[a b][x]'
    expect "a library that fails validation gets Proton's fallback" "$(lib "$WORK/Bad" hélper.exe module | tr -d '\r')" 'S:\common\Game\hélper.exe'
    expect "a console target gets its own console" "$(lib "$SLIB" helper-console.exe console "$(winpath "$WORK/con.txt")" > /dev/null; tr -d '\r' < "$WORK/con.txt")" own_console=1
    expect "and its exit code still reaches Steam" "$(lib_status "$SLIB" helper-console.exe exit 9)" 9
    expect "a .bat target runs (ShellExecute) and exits 0" "$(rm -f "$WORK/game dir/marker.txt"; L waitforexitandrun "$WORK/game dir/run.bat" > /dev/null 2>&1; echo $?; tr -d '\r' < "$WORK/game dir/marker.txt")" "0
    marker"
    expect "Source SDK's -game path is converted" "$(steam_sdk args -game /Volumes/x/mod | tr -d '\r')" '[-game][Z:\Volumes\x\mod]'
    expect "a link2ea:// target clears EA's IsUnavailable and exits 0" "<see below>" "0 IsUnavailable=0x0"
    ```
    - **`$WORK/Bad`** holds `SteamApps/common/Game` (wrong case), and `STEAM_COMPAT_LIBRARY_PATHS="$WORK/Bad/SteamApps"`.
    - **`run.bat`** is ASCII only: `@"%~dp0helper.exe" marker "%~dp0marker.txt"`, with an ASCII-named `helper.exe`
      copy beside it in `$WORK/game dir`. Wine's cmd reads batch files in the OEM code page.
    - **`steam_sdk`** runs `steam` with the helper copied to `$WORK/Source SDK Base 2007/helper.exe`. It is a direct
      row, so the target is a `Z:` path in quotes, which `fixup_cmdline_paths` needs.
    - **The EA row:**
      1. Seed `HKLM\Software\Electronic Arts\EA Desktop` with `IsUnavailable` as `REG_DWORD 1`.
      2. Run `"$WINE" "$STEAMEXE" 'link2ea://launchgame/1'`. That is not a command (handler rules, Task 5), so before
         Task 5 it is a game target. After Task 5 it still is: `link2ea` isn't `steam://`.
      3. Print the exit status and `reg query … /v IsUnavailable`.
  - **Existing rows:** row `:147-158` is rewritten as Proton's rule (R4):
    1. `missing.exe` alone leaves a dead `pid`;
    2. `steam steam` then shows `alive=1`, because the last steam.exe owns `pid`.

    Its comment no longer cites `unregister_steam`. The "pid cleared afterwards" rows (`:77`, `:127`) are removed.
  - **LauncherTests `:240`:** the argument becomes the raw `/Steam Library/My Game/Game.exe`.
- [ ] **Step 2: Run them to see them fail.**
  - `swift test --filter LauncherTests` fails at `:240`.
  - `make bridge-check`: the S: rows fail (`Z:\…`), as do the fallback, -game and EA rows.
  - The console, exit-code and `.bat` rows are regression rows; they may already pass.
- [ ] **Step 3: Implement**, from `proton-steam-c.md` §1-§3: the Makefile; `run_process`; `main`'s R4 order; the helper
  modes; and the `Launcher` change.
- [ ] **Step 4: Run them to see them pass.** `swift test`, `make bridge-check`, `sh wine-arm64/check.sh steam-bridge`.
  The last one is the probe's direct and launcher rows with GUI programs; skip it if Steam isn't running.
- [ ] **Step 5: Commit.** `steam.exe: Proton's run_process (Unix paths to S:, console targets, ShellExecute, EA, -game),
  GUI build, and the launcher's raw target`.

### Task 3: `main` in Proton's order, under the `SteamGameId` gate (P1-1 step 4, L2, L7, R11, R12, R18, R20)

**Files:**
- Modify:
  - `bridge/steam.c`: `main(int argc, char **argv)`.
  - `Sources/MacNeutronCore/Launcher.swift`: `addSteamClient`, `:192-197`, and `redactedKeys`, `:202`.
  - `Sources/MacNeutronCore/SteamLocation.swift`: remove `activeAccountID` and `loginUsers` (`:29`, `:64`).
  - `bridge/tests/helper.c`: the `steam` mode drops `user=`; new mode `window`.
  - `bridge/check.sh`:
    - `export SteamGameId=4242` next to `WINEDEBUG` (`:42`), and `SteamGameId=4242` in `L()` (`:116`);
    - copy steam.exe to `drive_c/windows/system32/steam.exe` too in the direct set-up (`:45-47`).
  - `bridge/probe.sh`: remove `ACCOUNT` (`:6`, `:14`, `:59`).
- Test files:
  - `LauncherTests.swift`: `:243`; delete `accountFromLaunchOptionsWins`; `gameLogsHideTheSteamAccount`; and
    `makeFixture`'s loginusers file at `:25`.
  - `SteamBridgeTests.swift`: `:47-64`.
  - `Support.swift`: `loginUser`, `loginUsersFile` and the `loginUsers:` parameter.

**Interfaces:**
- `main`'s order (`proton-steam-c.md` §10), with the gate on `getenv("SteamGameId")`, where any value, empty included,
  counts:
  1. **The command handler:** a stub returning FALSE; Task 5 fills it.
  2. **The gate:**
     - both named events;
     - `GetDesktopWindow()`;
     - `CreateThread(create_steam_windows)`, which makes the hidden `vguiPopupWindow` "Steam" and `static` "SteamVR
       Status" windows and runs a message loop;
     - `pid`;
     - `SteamPath`;
     - `ValvePlatformMutex` from `GetModuleFileNameW` (R11);
     - today's keys: `SteamExe`, `language`, `Apps\<id>`, `SteamClientDll*`, Wow6432Node and the winebrowser `HKCR`,
       which Task 5 replaces;
     - `RegDeleteValueW(ActiveProcess, L"ActiveUser")` (R12);
     - `PROTON_WAIT_ATTACH` (R18);
     - `game_process = TRUE`.
  3. **For a game process:** `setup_vr_registry` (R20).
  4. **Start the game:**
     - the DIPC semaphores and thread, for a game process (R5);
     - `run_process`;
     - `ProcessWineMakeProcessSystem` (1000), for a game process.
  5. **Wait:** `FreeConsole`, then the wait.
  6. **Exit code:** as Proton (`proton-steam-c.md` §10 table). `STILL_ACTIVE` is possible, as in Proton.

- [ ] **Step 1: Write the failing rows.**
  - LauncherTests:
    - `:243` becomes `#expect(game.environment["MACNEUTRON_STEAM_ACCOUNT"] == nil)`;
    - `gameLogsHideTheSteamAccount` checks the remaining redactions.
  - check.sh:
    - remove `user=` from the `:72-73` and `:122-123` expectations;
    - row `:85-86` becomes the first row below;
    - new rows:
    ```sh
    expect "ValvePlatformMutex names the steam.exe that ran (system32)" "$("$WINE" 'C:\windows\system32\steam.exe' "$HELPER" env ValvePlatformMutex | tr -d '\r')" '[C:/windows/system32/steam.exe]'
    expect "a game process has Steam's hidden window" "$(steam window | tr -d '\r')" "window=1"
    "$WINE" reg delete 'HKCU\Software\Valve\Steam\ActiveProcess' /v pid /f > /dev/null 2>&1 || true
    expect "without SteamGameId nothing is set up" "$(env -u SteamGameId "$WINE" "$STEAMEXE" "$HELPER" pid | tr -d '\r') $(env -u SteamGameId "$WINE" "$STEAMEXE" "$HELPER" events | tr -d '\r') $(env -u SteamGameId "$WINE" "$STEAMEXE" "$HELPER" env ValvePlatformMutex | tr -d '\r')" "0 lock=0 ipc=0 []"
    "$WINE" reg add 'HKCU\Software\Valve\Steam\ActiveProcess' /v ActiveUser /t REG_DWORD /d 12345 /f > /dev/null
    expect "an earlier build's ActiveUser is removed" "$(steam exit 0; "$WINE" reg query 'HKCU\Software\Valve\Steam\ActiveProcess' /v ActiveUser > /dev/null 2>&1 && echo kept || echo gone)" gone
    ```
    - `window` polls `FindWindowW(L"vguiPopupWindow", L"Steam")` for up to 2 s.
    - The expected `C:/windows/system32/steam.exe` is a guess at Wine's letter case. If the first green run prints
      another case, record what Wine reports in the D-line and in the row. The row still fails at Step 2: today's
      constant is the `Program Files` path.
- [ ] **Step 2: Run them to see them fail.**
  - `swift test`: `:243` fails (the launcher sets the account).
  - `make bridge-check`: the mutex, window, no-`SteamGameId` and `ActiveUser` rows fail.
- [ ] **Step 3: Implement** the new `main`, then the Swift and probe.sh removals (L2).
- [ ] **Step 4: Run them to see them pass.**
  - `swift test`, `make bridge-check`.
  - The macOS check: the count from `lsappinfo list | /usr/bin/grep -c steam.exe` doesn't grow while `steam window`
    runs. That means no Dock item for the hidden windows; write the count down.
- [ ] **Step 5: Commit.** `steam.exe: Proton's main under the SteamGameId gate (hidden Steam window, ValvePlatformMutex,
  no ActiveUser)`.

### Task 4: The launcher runs `system32\steam.exe` (P1-1 step 1, M3, R10, R16)

**Files:**
- Modify:
  - `Sources/MacNeutronCore/SteamBridge.swift`: `steamExe` (`:7`) and `prefixFiles` (`:23-26`).
  - `Sources/MacNeutronCore/Launcher.swift`: `runGame`, `:155`.
  - `Sources/MacNeutronCore/PrefixManager.swift`: `removeSteamBridge`, `:255-261`.
  - `bridge/check.sh`.
- Test: `LauncherTests.swift`, `PrefixManagerTests.swift`.

**Interfaces:**
- `SteamBridge.steamExe = #"C:\windows\system32\steam.exe"#`.
- `SteamBridge.programFilesSteamExe = #"C:\Program Files (x86)\Steam\steam.exe"#`.
- `SteamBridge.steamExe(forGameID: String?) -> String`: `programFilesSteamExe` for "311210", "1985810" and "1549250";
  `steamExe` otherwise.
- `prefixFiles` adds `(layout.steamHelper, "drive_c/windows/system32/steam.exe")`.
- `removeSteamBridge` removes `steamclient64.dll` and `steamclient.dll` only (R10).

- [ ] **Step 1: Write the failing tests.** They compare the runner's arguments as string literals, so they compile.
  - `gameGoesThroughSteamExeWhenTheBridgeIsInstalled` expects `C:\windows\system32\steam.exe`.
  - `threeGamesRunSteamExeFromTheSteamFolder` loops over `SteamGameId` 311210, 1985810 and 1549250 (expecting the
    `Program Files` path) and 480 (expecting system32).
  - `steamBridgeIsCopiedIntoTheSteamFolder` also expects `drive_c/windows/system32/steam.exe`.
  - `escapeHatchKeepsSteamExeForSteamLinks`: after removal, both copies exist and both DLLs are gone.
  - check.sh, a launcher row (Review Focus 4):
    1. One bridge run, to write `HKCR`.
    2. Then `MACNEUTRON_NO_STEAM_BRIDGE=1 L waitforexitandrun "$GAME" url steam://store/480`.
    3. `opened` is `steam://store/480`. The `opened()` helper comes from Task 5; define it here first, as the
       existing `:98` poll made into a function.
  - check.sh, a launcher row: `lsteam env ValvePlatformMutex` → `[C:/windows/system32/steam.exe]`.
- [ ] **Step 2: Run them to see them fail.** `swift test --filter 'LauncherTests|PrefixManagerTests'`: four failures
  (`gameGoesThrough…`, `threeGames…`, `steamBridgeIsCopied…`, `escapeHatchKeeps…`). `make bridge-check`: both new rows
  fail.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them to see them pass.** `swift test`, `make bridge-check`.
- [ ] **Step 5: Commit.** `launcher: run C:\windows\system32\steam.exe, as Proton (three games from the Steam folder);
  keep steam.exe when the bridge is off`.

### Task 5: The command handler, in-place restart and `HKCR\steam` (P1-1 step 3, L6, R6-R9)

**Files:**
- Modify: `bridge/steam.c`, `bridge/tests/helper.c` (mode `restart <count file> <second exit>`), `bridge/check.sh`.
- Test: `bridge/check.sh`.

**Interfaces:**
- `static BOOL steam_command_handler(int argc, char **argv)`, Proton's `steam.c:652-748` with R8. It recognises what
  Proton does: no arguments, `steam://` (a case-insensitive prefix), or a first argument starting with `-`. Then:
  1. **The restart:** `--` followed by `steam://launch/<SteamGameId>` or `steam://rungameid/<SteamGameId>` → R6,
     return TRUE.
  2. Strip a leading `--`.
  3. `-applaunch <id>` → `steam://rungameid/<id>`.
  4. No arguments → `steam://open/main`.
  5. A `steam://…` URL → `forward_to_mac_steam(url)`. Other `-` options → `steam.exe: ignoring <arg> (Mac Steam has no
     command line)` on stderr.
  6. Return TRUE: exit 0.
- `static void forward_to_mac_steam(const char *url)`:
  - `CreateProcessW` of `C:\windows\system32\winebrowser.exe "<url>"`, waiting for it, with the URL converted ACP →
    wide;
  - never ShellExecute: `HKCR\steam` points back here.
- **The restart, waiting side:** Proton's `steam.c:1003-1049`, with R7.
  - The auto-reset event `PROTON_STEAM_EXE_RESTART_APP` is created after `run_process`.
  - It restarts only once the child has exited.
  - The exit code is the last child's.
- `HKCR\steam\shell\open\command` (default) = `"C:\Program Files (x86)\Steam\steam.exe" -- "%1"`, plus `URL Protocol`
  = "", under the gate (R9). This replaces Task 3's winebrowser value.

- [ ] **Step 1: Write the failing rows.** `opened()` exists (Task 4). Each row first runs `rm -f "$WORK/open.txt"`.
  ```sh
  expect "HKCR\\steam is Proton's handler" "$(steam exit 0; "$WINE" reg query 'HKCR\steam\shell\open\command' /ve | tr -d '\r' | sed -n 's/.*REG_SZ *//p')" '"C:\Program Files (x86)\Steam\steam.exe" -- "%1"'
  expect "steam.exe steam://store/480 opens Mac Steam" "$("$WINE" "$STEAMEXE" steam://store/480; echo $?; opened)" "0
  steam://store/480"
  expect "-applaunch reaches Mac Steam as rungameid" "$("$WINE" "$STEAMEXE" -applaunch 480; echo $?; opened)" "0
  steam://rungameid/480"
  expect "a game's steam:// link goes through HKCR to Mac Steam" "$(steam url steam://store/480 > /dev/null; opened)" steam://store/480
  expect "a game restarting itself through steam:// restarts in place" "$(rm -f "$WORK/starts.txt"; steam restart "$(winpath "$WORK/starts.txt")" 6; echo $?; tr -d '\r' < "$WORK/starts.txt"; opened)" "6
  2"
  ```
  The `restart <count file> <second exit>` mode (L6):
  1. It increments the count in the file.
  2. On the first start, it polls `OpenEventW(SYNCHRONIZE, …, "PROTON_STEAM_EXE_RESTART_APP")` until it succeeds (2 s
     at most).
  3. It then runs `C:\Program Files (x86)\Steam\steam.exe -- steam://rungameid/4242` with `CreateProcessW`, without
     waiting, and exits 0.
  4. On the second start, it exits with `<second exit>`.

  The empty fifth line asserts that Mac Steam was not asked. The existing row `:95-99` is replaced by the fourth row.
- [ ] **Step 2: Run them to see them fail.** `make bridge-check`:
  - the `HKCR` row fails (winebrowser value);
  - `-applaunch` fails (it starts a program);
  - the restart count is 1.
  - The `steam://store` rows may already pass through the old handler; they are regression rows.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run them to see them pass.** `make bridge-check` three times in a row, all green. The restart row is
  timing-bound, so a flake here is a bug, not noise.
- [ ] **Step 5: Commit.** `steam.exe: Proton's command handler and in-place restart; steam:// to Mac Steam through
  winebrowser (D-12)`.

### Task 6: `libraryfolders.vdf` from steam.exe, with S: (P0-3 step 4)

**Files:**
- Modify:
  - `bridge/steam.c`: `setup_steam_files` and `escape_path_unix_to_dos`, in the gate after the registry block.
  - `Sources/MacNeutronCore/PrefixManager.swift`: delete `writeSteamFiles` (`:239-250`) and its call (`:99`).
  - `Sources/MacNeutronCore/SteamBridge.swift`: delete `libraryFolders` (`:28-42`) and `windowsPath` (`:10-12`).
  - `bridge/check.sh` (`:134-144`).
  - Tests: delete `steamFilesComeWithTheBridge` (PrefixManagerTests `:223-233`),
    `libraryFoldersListsEachLibraryUnderItsParent` (SteamBridgeTests `:25-31`) and `windowsPathMapsUnixPathsToDriveZ`
    (`:5-8`).

**Interfaces:**
- `static void setup_steam_files(void)`, Proton's `steam.c:750-816`, with two deviations:
  - D-8: leave out the entry-0 `STEAM_COMPAT_CLIENT_INSTALL_PATH` block. On macOS that is the bundle's MacOS folder,
    not a library.
  - A bug-fix deviation: skip empty `STEAM_COMPAT_LIBRARY_PATHS` entries. Proton leaves the buffer uninitialised
    there.

- [ ] **Step 1: Write the failing rows.** check.sh `:134-144`, through `lib "$SLIB" …`, with three libraries:
  `$SLIB/steamapps`, `$WORK/library ü/steamapps` and `/Volumes/Game Drive/SteamLibrary/steamapps`. The vdf lists:
  - index 0: `"path"		"S:\\"`;
  - index 1: `$WORK/library ü`'s escaped `Z:\\…` path, which covers the UTF-8 conversion;
  - index 2: `"Z:\\Volumes\\Game Drive\\SteamLibrary"`.

  `Steam\config` exists. A run without `SteamGameId` writes nothing:
  ```sh
  rm -f "$STEAM/steamapps/libraryfolders.vdf"; env -u SteamGameId STEAM_COMPAT_LIBRARY_PATHS="$SLIB/steamapps" "$WINE" "$STEAMEXE" "$HELPER" exit 0
  expect "without SteamGameId no libraryfolders.vdf" "$([ -e "$STEAM/steamapps/libraryfolders.vdf" ] && echo yes || echo no)" no
  ```
- [ ] **Step 2: Run them to see them fail.** `make bridge-check`: the vdf row fails (the launcher's `Z:` text).
- [ ] **Step 3: Implement**, then delete the Swift code and tests.
- [ ] **Step 4: Run them to see them pass.** `swift test`, `make bridge-check`.
- [ ] **Step 5: Commit.** `steam.exe: write libraryfolders.vdf as Proton's setup_steam_files (S:), not the launcher`.

### Task 7: Deviations, docs, gates, and the maintainer's smoke (Part A item 2 gate)

**Files:**
- Modify:
  - `docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md`:
    - replace the stale §2.8 and §3 diagram facts (parity spec part 4);
    - add every D-line from R1-R20, and from part 4's D-1 to D-20 those this plan touches;
    - drop D-13a (R11);
    - add `STILL_ACTIVE` as Proton behaviour.
  - `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md`: amend Part B P0-3 step 1 (R2), P1-1 step
    1, and the A4 removal note (R10), each pointing to this plan's ruling.
  - `README.md`, "Per-game options": a row for `/usr/bin/env PROTON_SET_GAME_DRIVE=0 %command%` (the game sees its old
    `Z:` path), and the note that `MACNEUTRON_NO_STEAM_BRIDGE=1` no longer changes the path.
- Create: `docs/testing/acceptance-parity-s-drive-steam-exe.md`.

- [ ] **Step 1: Gates.** Redirect output to `build/parity/logs/` and read the tails:
  - `swift test`;
  - `make bridge-check`;
  - `make smoke`: console exes through the launcher, now with their own consoles; exit codes as before;
  - `sh wine-arm64/check.sh steam-bridge`.
- [ ] **Step 2: Dev app.** `make app APP=build/dev/MacNeutron.app` with the signing variables.
- [ ] **Step 3: Write the acceptance doc** with the gate results and the maintainer's rows:
  1. **Palworld:**
     - it starts and loads a world;
     - its game log (with `MACNEUTRON_LOG=1`) shows `S:\steamapps\common\Palworld\…`;
     - SteamStub passes.
  2. **SMITE 2:** one match.
  3. **One launcher-based game whose prefix last ran under the `Z:` build** (the maintainer names it): it starts through
     its launcher. Record whether the launcher asks to repair or relocate.
  4. **Timberborn** (passthrough): unchanged.
  5. **Store link:** an in-game store link opens Mac Steam.
  6. **No Dock icon:** none appears for steam.exe at launch, and no console window opens for the three games.
  7. **Opt-out:** `/usr/bin/env PROTON_SET_GAME_DRIVE=0 %command%` on one game shows `Z:` again.

  Part A item 2: P0-3 stays P0 only if rows 1-3 pass; otherwise it is recorded as P1.
- [ ] **Step 4: Privacy scan of the added lines, then commit.**
  - Scan: `git diff --cached -U0 -- <docs> | /usr/bin/grep '^+' | /usr/bin/grep -nE "/Users/[^<]|[0-9]{17}"`; it must
    print nothing.
  - Don't paste raw probe rows.
  - Commit: `docs: S: drive and steam.exe parity deviations, README and acceptance`.
- [ ] **Step 5: Ask the maintainer to run rows 1-7.** Record the results in the acceptance doc and commit them.
