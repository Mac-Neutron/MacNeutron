# Install Scripts in the VM Prefix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A game launched in the VM gets its Steam install scripts run in its prefix before the game, as Linux Steam runs them through Proton.

**Architecture:**
- The launcher reads Steam's app manifests and the scripts they list (`InstallScripts.swift`) and puts a plan into the
  guest's launch script: quoted shell facts in front of run-game.sh.
- run-game.sh imports the scripts' registry entries when they change. It runs each "Run Process" step unless the
  prefix's registry already has its HasRunKey value, and marks the step after exit 0.
- Proton's nine CommonRedist marks go into run-game.sh's one-time registry block.

**Tech Stack:** Swift 6 (MacNeutronCore, swift-testing), POSIX sh in the guest (Fedora image: coreutils, grep, sed;
**no awk**), Wine's `reg`, `cmd` and `msiexec`, llvm-mingw for the gate's test program.

**Spec:** `docs/superpowers/specs/2026-10-24-macneutron-install-scripts-design.md` (approved 2026-10-24: approach B,
Proton's marks in run-game.sh's registry block).

## Global Constraints

- Only `waitforexitandrun` launches that run in the VM get scripts; `run`, `getcompatpath` and macOS launches don't.
- HasRunKey and Registry keys under `HKEY_LOCAL_MACHINE\SOFTWARE\` map to `HKEY_LOCAL_MACHINE\Software\Wow6432Node\`;
  other HKLM keys and all HKCU keys keep their path.
- A step runs when its DWORD (named after the entry) is missing or below `MinimumHasRunValue`. The minimum defaults
  to 1. Exit 0 writes the minimum; any other status writes nothing.
- `%INSTALLDIR%` is `Z:\mnt\games\<installdir of the script's own app>`, with no trailing backslash.
- The token table is spec §4.1. Any other `%NAME%` skips the entry or value, with a note.
- Every value from a script reaches the guest single-quoted by `MicroVM.quoted`, and nothing is `eval`ed.
- Proton's nine marks are exactly spec §2's: `.NET\4.8`, `4.7.2`, `4.6.2`, `4.5.2`, `4.0`, `4.0 Client Profile`, `3.5`,
  `3.5 Client Profile` and `XNA\4.0`. Each sits under `HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\`,
  with a value named after its last component, `dword:00000001`.
- Standing rules:
  - Commit locally with explicit paths only; never push.
  - Launcher tests use a temp `MACNEUTRON_VM_DIR` or `MACNEUTRON_VM=0`.
  - Guest gates use `BUILD_DIR=~/Documents/MacProton/build`, the main checkout's dev VM. If another session holds the
    dev VM (the builder or an image rebuild), ask it before booting.
  - Never open MacNeutron.app, and never start or quit Steam. The 3DMark loop exception allows
    `open steam://rungameid/231350` while Steam runs.
  - Write `~`, not the home path, in docs.
- Lighter testing: each task runs its own tests. The full gates come once, in Task 4.

## Review Focus

1. **A script file with a UTF-8 BOM or CRLF line ends** (Steam's files are CRLF) is planned like any other, not
   skipped as unparsable. Test: Task 1 `aBOMAndCRLFScriptIsRead`.
2. **A game whose shared redistributables live in no app manifest of its library**, because the shared app isn't
   installed there yet: its own scripts are still planned, and the missing ones get one note each. Test: Task 1
   `missingManifestsAndScriptsAreSkipped`.
3. **A non-ASCII or apostrophe-bearing game folder** (`Jeu d'été`) keeps its exact bytes in the process path, folder
   and registry text, and the guest gets them as one word each. Test: Task 2 `theShellQuotesEveryWord`.
4. **A registry value, or a HasRunKey name, containing `"` or `\`** is escaped the way Wine writes `.reg` files, so the
   import and run-game.sh's `mn_hasrun` agree. Test: Task 1 `registryTextEscapesAsRegFilesDo`. The guest gate's step
   `g` has a backslash-free name, so mn_hasrun's escaping is checked on the Swift side only.
5. **An installer that waits for a click on the guest's screen, which nobody sees** (Xvfb; nothing presents it on the
   Mac), must not hold the game forever. Each step gets `${MN_INSTALL_TIMEOUT:-900}` seconds. A timeout is a failure:
   it's logged, nothing is marked, the step is retried at the next launch, and `wineserver -k` ends what the installer
   left running. This is an addition to the spec (§4.5 gains it in Task 3). Test: Task 3 gate step `h`.

---

### Task 1: `InstallScripts.plan`, from Steam's manifests to the steps

**Files:**
- Create: `Sources/MacNeutronCore/InstallScripts.swift`
- Create: `Tests/MacNeutronCoreTests/InstallScriptsTests.swift`

**Interfaces:**
- Consumes: `KeyValues.parse(_:) -> [KVNode]`, `[KVNode].node(at:)` (case-insensitive), `KVNode.stringValue`,
  `KVNode.children`, `KeyValues.unescape(_:)` (`parse` keeps `\\` as written).
- Produces:

```swift
/// Steam's install scripts for one game in the VM (spec 2026-10-24-macneutron-install-scripts-design §4.1).
struct InstallScripts: Equatable {
    /// A "Run Process" entry: run-game.sh's mn_run runs `process` in `folder` unless `key`'s DWORD `name` is at least
    /// `minimum`, and writes `minimum` there after exit 0.
    struct Run: Equatable {
        let key: String, name: String, minimum: Int
        let folder: String      // the process's folder in the guest, a Unix path: its working directory
        let process: String     // Windows path, ending .exe, .cmd, .bat or .msi
        let arguments: [String]
    }
    var registry = ""           // REGEDIT4 text of every Registry entry, "" when none
    var runs: [Run] = []
    var notes: [String] = []    // one per thing skipped, for launcher.log
    static func plan(steamapps: URL, appID: String) -> InstallScripts
    static func arguments(_ command: String) -> [String]
    static func registryKey(_ path: String) -> String?
}
```

The tests build a library in `makeTempDir()`:
- `steamapps/appmanifest_231350.acf`: `installdir` "3DMark Demo", `InstallScripts` { 223851 `installscript.vdf`,
  231351 `installscript_si.vdf` }, `SharedDepots` { 228984, 228990, 229004 → 228980 }.
- `steamapps/appmanifest_228980.acf`: `installdir` "Steamworks Shared", `InstallScripts` for 228984, 228990 and 229004,
  with the paths of spec §2 (`_CommonRedist\\vcredist\\2012\\installscript.vdf`, ...).
- The five scripts as `steamapps/common/...` files, copied verbatim from spec §2's printouts (`kvsignatures` included).

- [ ] **Step 1: Write the failing tests**

`threeDMarksScriptsBecomeItsPlan`: `plan(steamapps:appID: "231350")` gives `runs`, in this order:
- VC 2012 `x86 Update 4`: key `#"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\vcredist\2012"#`,
  minimum 1, folder `"/mnt/games/Steamworks Shared/_CommonRedist/vcredist/2012"`, process
  `#"Z:\mnt\games\Steamworks Shared\_CommonRedist\vcredist\2012\vcredist_x86.exe"#`, arguments `["/quiet", "/norestart"]`.
- `x64 Update 4`: the same, with `vcredist_x64.exe`.
- `dxsetup`: `...\DirectX\Jun2010\DXSETUP.exe`, `["/silent"]`.
- `.NET 4.5.2`: the non-XP entry, `...\DotNet\4.5.2\Microsoft .NET Framework 4.5.2.cmd`, `[]`.
- `FmSysInfo`: key `#"HKEY_CURRENT_USER\SOFTWARE\Valve\Steam\Apps\231350"#`, minimum 1517, folder
  `"/mnt/games/3DMark Demo"`, process `#"Z:\mnt\games\3DMark Demo\SystemInfoSetupAssistant.exe"#`, arguments
  `["Futuremark_SystemInfo_5_93_1517.msi"]`.

`registry` is
`"REGEDIT4\n\n[HKEY_LOCAL_MACHINE\\Software\\Wow6432Node\\Futuremark\\3DMark]\n\"Install_Path\"=\"Z:\\\\mnt\\\\games\\\\3DMark Demo\"\n"`
and `notes` is empty.

Further tests:
- `keysMatchInAnyCase`: a script whose keys are `installscript`, `run process`, `hasrunkey`, `process 1`, `command 1`
  and `requirement_os` gives the same `Run` as its capitalised twin.
- `anEntryForAnotherWindowsIsDropped`: entries with `Is64BitWindows` "0", `OSType` "Windows XP" and
  `Requirement_OS { "IsServer" "1" }` are not in `runs`, and the last gets a note containing "IsServer". An entry with
  `OSType` "windows 10" is kept.
- `theFirstEntryPerHasRunValueWins`: two applicable entries named `4.5.2` under one HasRunKey give one `Run`, the
  first.
- `whatIsntSupportedIsSkippedWithANote`: `runs` and `registry` stay empty, and there's a note containing each of
  `Firewall`, `Run Process On Uninstall`, `HasRunKey`, `process 2`, `%ROOTDRIVE%`, `.ps1`, `language`. The script has:
  a Firewall section; a Run Process On Uninstall section; an entry without HasRunKey; one with `process 2`; one whose
  process uses `%ROOTDRIVE%`; one whose process is a `.ps1`; a Registry `"string" { "english" { ... } }`.
- `missingManifestsAndScriptsAreSkipped`:
  - No `appmanifest_231350.acf` gives `InstallScripts()`, with no notes (a launch from outside Steam).
  - Without `appmanifest_228980.acf`, only 3DMark's own run and registry are planned, plus one note per shared depot.
  - A listed script file that's missing gives a note naming its path.
- `aBOMAndCRLFScriptIsRead`: `installscript_si.vdf` written with a leading U+FEFF and `\r\n` gives the same
  `FmSysInfo` run.
- `registryTextEscapesAsRegFilesDo`: a `string` value `a"b\c` comes out as `"v"="a\"b\\c"`. A `dword` "1517" comes out
  as `"d"=dword:000005ed`, and `(Default)` as `@="x"`. A `dword` that isn't a decimal number, and a value holding a
  newline, are skipped with a note.
- `windowsCommandLinesSplitAsWindowsDoes`: `arguments` gives these results:
  - `"/quiet /norestart"` → `["/quiet", "/norestart"]`
  - `#"install "a b" c"#` → `["install", "a b", "c"]`
  - `#"a\"b"#` → `[#"a"b"#]`
  - `#"a\\"b c""#` → `[#"a\b c"#]`
  - `#""""#` → `[""]`
  - `"  "` → `[]`
- `hklmSoftwareIsSteamsThirtyTwoBitView`: `registryKey` gives these results:
  - `#"HKEY_LOCAL_MACHINE\SOFTWARE\Futuremark"#` → `#"HKEY_LOCAL_MACHINE\Software\Wow6432Node\Futuremark"#`
  - `#"HKLM\Software\X"#` → `#"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X"#`
  - `#"HKEY_LOCAL_MACHINE\Software\Wow6432Node\X"#` stays as it is
  - `#"HKEY_LOCAL_MACHINE\SYSTEM\X"#` stays as it is
  - `#"HKCU\Software\X"#` → `#"HKEY_CURRENT_USER\Software\X"#`
  - `#"HKEY_CLASSES_ROOT\X"#` → `nil`

- [ ] **Step 2: Run the tests to see them fail**

Run: `swift test --filter InstallScriptsTests`
Expected: build failure, `cannot find 'InstallScripts' in scope`.

- [ ] **Step 3: Implement `InstallScripts` in `Sources/MacNeutronCore/InstallScripts.swift`**

- Scripts are ordered as spec §4.1 says: the game's `SharedDepots` order, then its `InstallScripts` order.
- Read files with `String(contentsOf:encoding: .utf8)` and drop a leading `"\u{FEFF}"`. A file that doesn't parse gets
  a note.
- Unescape every key and value before using it. Script paths in manifests use `\`: turn them into `/` under the app's
  `installdir`.
- Map a process path to its Unix folder: `Z:\a\b\x.exe` → `/a/b`, and `C:\a\x.exe` → `/home/mn/pfx/drive_c/a`.
- Dedup on the lowercased `(key, name)` pair.
- Write notes as `"<script path>: <entry or section>: <why>"`.
- `arguments` follows CommandLineToArgvW. Whitespace outside quotes separates arguments, and `"` toggles quoting.
  2n backslashes before a `"` give n backslashes and a toggle; 2n+1 give n backslashes and a literal `"`. Other
  backslashes are literal.
- Add a `ponytail:` comment naming what's left out (language blocks, `process 2+`, `%ROOTDRIVE%`, other requirement
  keys): it's added when a game needs it.

- [ ] **Step 4: Run the tests to see them pass**

Run: `swift test --filter InstallScriptsTests`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/InstallScripts.swift Tests/MacNeutronCoreTests/InstallScriptsTests.swift
git commit -m "MacNeutronCore: InstallScripts plans a game's Steam install scripts from its app manifests" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 2: The plan reaches the guest, and its results reach launcher.log

**Files:**
- Modify: `Sources/MacNeutronCore/InstallScripts.swift` (add `shell`)
- Modify: `Sources/MacNeutronCore/MicroVM.swift` (`script(...)`)
- Modify: `Sources/MacNeutronCore/Launcher.swift` (`runInMicroVM`)
- Test: `Tests/MacNeutronCoreTests/InstallScriptsTests.swift`, `Tests/MacNeutronCoreTests/MicroVMTests.swift`

**Interfaces:**
- Consumes: Task 1's `InstallScripts.plan(steamapps:appID:)`, `.runs`, `.registry`, `.notes`; `MicroVM.quoted`.
- Produces:
  - `var shell: String` on `InstallScripts`. It's `""` when `registry` and `runs` are both empty. Otherwise, with
    `q` = `MicroVM.quoted`, it's:
    - `MN_INSTALL_REG=<q(registry)>\n`, only when `registry` isn't empty;
    - `mn_install_script() {\n`;
    - one `  mn_run <q key> <q name> <q String(minimum)> <q folder> <q process> <q each argument>\n` line per run;
    - `}\n`, after zero or more runs.
  - `MicroVM.script(verb:appID:guest:arguments:environment:performance:installScript: String = "")` puts
    `installScript` after the facts and their `export` line, just before run-game.sh's text.
  - Task 3's run-game.sh and gate rely on the names `MN_INSTALL_REG`, `mn_install_script` and
    `mn_run <key> <name> <minimum> <folder> <process> <args...>`.
  - The guest's result file is the run share's `traces/installscript.log`, one `<name> exit <status>` line per step it
    ran.

- [ ] **Step 1: Write the failing tests**

- `theShellQuotesEveryWord` (InstallScriptsTests): take a `Run` with key `#"HKEY_CURRENT_USER\Software\T"#`, name `n`,
  minimum 1, folder `/mnt/games/Jeu d'été`, process
  `#"Z:\mnt\games\Jeu d'été\setup.exe"#` and arguments `["/S", "it's"]`, and a registry `"REGEDIT4\n\n[X]\n"`. `shell`
  is exactly:

```
MN_INSTALL_REG='REGEDIT4

[X]
'
mn_install_script() {
  mn_run 'HKEY_CURRENT_USER\Software\T' 'n' '1' '/mnt/games/Jeu d'\''été' 'Z:\mnt\games\Jeu d'\''été\setup.exe' '/S' 'it'\''s'
}
```

  `InstallScripts().shell == ""`.
- `aGamesInstallScriptsReachTheGuest` (MicroVMTests): use a library in a temp folder, with
  `steamapps/common/Game/game.exe` and an `appmanifest_2437170.acf` whose `installscript.vdf` has one Run Process entry
  and one Registry value.
  - A `waitforexitandrun` launch's script contains `"mn_install_script() {\n"` and `"MN_INSTALL_REG='"`, both before
    `"# run-game.sh"`.
  - A `run` launch of the same exe has neither.
- `installScriptResultsReachLauncherLog` (MicroVMTests): the fake run-guest.sh writes
  `traces/installscript.log` = `"x86 Update 4 exit 0\nFmSysInfo exit 1\n"` under `MN_SHARE`.
  - launcher.log contains `"install script 2437170: x86 Update 4 exit 0"` and `"install script 2437170: FmSysInfo exit 1"`.
  - With `version: "0.7.1p"`, only the `exit 1` line is there (a summary line; successes are `append`).
  - An earlier run's file left in `traces/` before the launch isn't reported.
- `installScriptNotesReachLauncherLog` (MicroVMTests): a script with a Firewall section puts a line containing
  `"install script 2437170: "` and `"Firewall"` into launcher.log.

- [ ] **Step 2: Run the tests to see them fail**

Run: `swift test --filter "InstallScriptsTests|MicroVMTests"`
Expected: build failure on `shell` and `installScript:`.

- [ ] **Step 3: Implement**

- `shell` as specified above.
- In `runInMicroVM`:
  - For `waitforexitandrun` only: `InstallScripts.plan(steamapps: URL(filePath: library, directoryHint: .isDirectory).deletingLastPathComponent(), appID: context.appID)`.
  - Each note goes to `log.append("install script \(appID): \(note)")`.
  - Pass `shell` to `vm.script`.
  - Remove `traces/installscript.log` before the boot, next to the `mounts.failed` removal.
  - After the boot, read it. A line ending `" exit 0"` goes to `log.append`; any other line goes to `log.summary`,
    each prefixed `"install script \(appID): "`.

- [ ] **Step 4: Run the tests to see them pass**

Run: `swift test --filter "InstallScriptsTests|MicroVMTests"`
Expected: all pass, the existing MicroVMTests included. Their fake game path has no manifest, so they get no scripts
and no notes.

- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/InstallScripts.swift Sources/MacNeutronCore/MicroVM.swift Sources/MacNeutronCore/Launcher.swift Tests/MacNeutronCoreTests/InstallScriptsTests.swift Tests/MacNeutronCoreTests/MicroVMTests.swift
git commit -m "launcher: a VM game's install scripts go to the guest, their results to launcher.log" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 3: run-game.sh runs them; Proton's marks; the guest gate

**Files:**
- Modify: `eac-vm/guest/run-game.sh`: the registry block (≈ lines 73-88), and a new part before
  `: > /mnt/share/traces/ready`.
- Create: `eac-vm/guest/win/exitcode.c`
- Modify: `eac-vm/guest/build-win-tests.sh` (build `exitcode.exe`, and name it in the header comment)
- Create: `eac-vm/tests/install-scripts.sh`
- Modify: `docs/superpowers/specs/2026-10-24-macneutron-install-scripts-design.md` §4.5 (the timeout, Review Focus 5)

**Interfaces:**
- Consumes: Task 2's `MN_INSTALL_REG`, `mn_install_script` and the `mn_run` argument order.
- Produces:
  - `traces/installscript.log` lines `<name> exit <status>`, read by Task 2's launcher code.
  - `exitcode.exe <code|hang> <file>`: it appends `ran\r\n` to `<file>`, then returns `<code>`, or with `hang` sleeps
    forever.

- [ ] **Step 1: Write the gate `eac-vm/tests/install-scripts.sh`**

Follow `fonts.sh`'s pattern:
- A fresh `state/install-scripts.img`.
- The guest script is the facts (hand-written in Task 2's `shell` format) followed by the worktree's run-game.sh.
- The "game" is `cmd.exe /c 'Z:\mnt\share\install scripts\check1.bat'`, which exits 7 when all is well.
- There are two boots; the second runs `check2.bat`.
- Set `MN_INSTALL_TIMEOUT=10` in the facts.
- Under `$W/share/install scripts/` (a path with a space): `exitcode.exe` (from `share/bin`), `Valve 3010.cmd` and
  `Valve 1.cmd`. Each `.cmd` is Valve's VC 2022 wrapper, CRLF:
  `@ECHO OFF` / `start /w "" "%~dp0\exitcode.exe" <3010|1> "%~dp0\ran-<c|d>.txt"` / `IF %ERRORLEVEL% == 3010 EXIT /B 0` /
  `EXIT /B %ERRORLEVEL%`.

| Step | key (`M` = `HKEY_LOCAL_MACHINE\Software\Wow6432Node\MNTest`) | name | min | process | runs over 2 boots | value after |
|---|---|---|---|---|---|---|
| a | M | a | 1 | `exitcode.exe 0 ran-a.txt` | 1 | 0x1 |
| b | M | b | 1 | `exitcode.exe 1 ran-b.txt` | 2 | none |
| c | M | c | 1 | `Valve 3010.cmd` | 1 | 0x1 |
| d | M | d | 1 | `Valve 1.cmd` | 2 | none |
| e | M | e | 5 | `exitcode.exe 0 ran-e.txt` | 1 | 0x5 |
| f | `...\Wow6432Node\Valve\Steam\Apps\CommonRedist\.NET\4.0` | 4.0 | 1 | `exitcode.exe 0 ran-f.txt` | 0 (Proton's mark) | 0x1 |
| g | `HKEY_CURRENT_USER\Software\MNTest` | g | 1 | `exitcode.exe 0 ran-g.txt` | 1 | 0x1 |
| h | M | h | 1 | `exitcode.exe hang ran-h.txt` | 2 (each timed out) | none |

- `MN_INSTALL_REG` is `[M\Reg]` with `"Install_Path"="Z:\\mnt\\games\\x"`.
- `check1.bat` uses `reg query ... | find`, one distinct exit code per failed check: a, c, e, f and g have their
  values; b, d and h have none; `Install_Path` is there; and XNA's `4.0` mark is there. Then it runs
  `reg add ... Install_Path /d changed /f` and exits 7.
- `check2.bat` checks that `Install_Path` is still `changed` (the unchanged registry text wasn't imported again) and
  that e is still 0x5, then exits 7.
- On the host, after both boots:
  - The `ran` line counts per file match the table.
  - `traces/installscript.log` from boot 2 has `b exit 1`, `d exit 1` and `h exit 124`, and no a, c, e, f or g line.
  - Both boots exited 7, so the game started after a hung step.
- Print `install-scripts: ok`, or `install-scripts: FAIL <what>` and exit 1.

- [ ] **Step 2: Run the gate to see it fail**

Run: `sh eac-vm/guest/build-win-tests.sh` (after adding `exitcode.c`), then
`BUILD_DIR=~/Documents/MacProton/build sh eac-vm/tests/install-scripts.sh`
Expected: `install-scripts: FAIL status ...`. Today's run-game.sh defines no `mn_run`, so the facts' function call
fails and check1 finds no values.

- [ ] **Step 3: Implement in `eac-vm/guest/run-game.sh`**

- **The registry block:**
  - Add `! grep -qF 'CommonRedist\\XNA\\4.0]' "$WINEPREFIX/system.reg"` to its condition.
  - Add the nine marks (Global Constraints) to the `.reg` it imports, as
    `[HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\<path>]` / `"<name>"=dword:00000001`.
  - Add a comment line citing Proton's `[ProtonOverrides]`.
  - The block's `wineserver -k` becomes `wineserver -k; wineserver -w`. Then the marks are on disk before `mn_hasrun`
    reads `system.reg` (gate row f). Make the change there, before anything else starts Wine. Don't put a bare
    `wineserver -w` later: by then `prefix.sh` or a `wine` call may have started a server whose services never exit.
- **Before `: > /mnt/share/traces/ready`:** a commented part that defines `mn_hasrun` and `mn_run`. When
  `MN_INSTALL_REG` is set or `mn_install_script` is defined, it does three things:
  - `mkdir -p "$WINEPREFIX/drive_c/users/steamuser/AppData/Local/Temp"`.
  - It writes `MN_INSTALL_REG` to `/tmp/mn-installscript.reg`. If `cmp -s` finds it differs from
    `$WINEPREFIX/.mn-installscript.reg`, it runs `wine reg import 'Z:\tmp\mn-installscript.reg'` and copies the file
    there.
  - It calls `mn_install_script`.

`mn_hasrun <key> <name>` prints the DWORD's hex digits, or nothing. The guest has no awk, so it uses fixed strings only
(no regex escaping):

```sh
mn_hasrun() {
  case $1 in HKEY_LOCAL_MACHINE\\*) f=$WINEPREFIX/system.reg;; *) f=$WINEPREFIX/user.reg;; esac
  k=$(printf '%s' "${1#*\\}" | sed 's/\\/\\\\/g')
  n=$(printf '%s' "$2" | sed 's/\\/\\\\/g; s/"/\\"/g')
  l=$(grep -n -i -F "[$k] " "$f" 2>/dev/null | grep '^[0-9]*:\[' | head -1 | cut -d: -f1)
  [ -n "$l" ] && tail -n "+$((l + 1))" "$f" | sed '/^\[/q' | grep -i -F "\"$n\"=dword:" | head -1 | sed 's/.*=dword://'
}
```

`mn_run <key> <name> <minimum> <folder> <process> <args...>` does this:
- It returns when `$((0x${v:-0})) -ge minimum`.
- It picks the command by lowercased extension: `.exe` → `wine <process> <args>`, `.cmd`/`.bat` →
  `wine cmd /c <process> <args>`, `.msi` → `wine msiexec /i <process> <args>`.
- It runs that in `(cd <folder> && timeout "${MN_INSTALL_TIMEOUT:-900}" ...)`, with output to `/dev/null`.
- On status 124 it runs `wineserver -k`.
- It appends `<name> exit <status>` to `/mnt/share/traces/installscript.log`.
- On status 0 it runs `wine reg add <key> /v <name> /t REG_DWORD /d <minimum> /f`.
- Its `ponytail:` comment says Wine's status is the code mod 256.

Notes for the implementer:
- run-game.sh is one shell, and the fonts loop already uses `f`, so declare the functions' variables `local` (the
  image's sh is bash).
- run-game.sh's own comment says the `wine` command can restart a program through start.exe. If gate row b gets
  marked, or row h leaves `exitcode.exe` running after the `wineserver -k`, run `.exe` steps the way the
  `waitforexitandrun` branch does: `WINELOADERNOEXEC=1 "$L/wine-preloader" "$L/wine" <process> <args>`.
- Wine's `reg import` reads a REGEDIT4 file in the ANSI code page, so a non-ASCII registry value survives only under a
  UTF-8 guest locale. No game here has one; leave it as a `ponytail:` note.

In the spec's §4.5, add the timeout bullet, worded as Review Focus 5.

- [ ] **Step 4: Run the gate and its neighbours to see them pass**

Run, with `BUILD_DIR=~/Documents/MacProton/build`: `sh eac-vm/tests/install-scripts.sh`,
`sh eac-vm/tests/fonts.sh` (same registry block) and `sh eac-vm/tests/game-exit.sh` (a game without scripts).
Expected: `install-scripts: ok`, `fonts: ok`, `game-exit: ok`.

- [ ] **Step 5: Commit**

```bash
git add eac-vm/guest/run-game.sh eac-vm/guest/win/exitcode.c eac-vm/guest/build-win-tests.sh eac-vm/tests/install-scripts.sh docs/superpowers/specs/2026-10-24-macneutron-install-scripts-design.md
git commit -m "eac-vm/guest: run-game.sh runs a game's Steam install scripts, with Proton's CommonRedist marks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

### Task 4: Docs, the milestone gates and the maintainer's 3DMark check

**Files:**
- Modify: `docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md`. Replace D-16 (≈ line 179) with spec
  §5's text, verbatim.
- Modify: `docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md`. Add a status line under **P1-4**:
  steps 1 and 3 are replaced by the install-scripts spec, step 2 is its §4.6 in the VM prefix, and step 4 is dropped.

- [ ] **Step 1: Edit both docs, then run a privacy scan**

Run: `git diff -U0 -- docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md | /usr/bin/grep '^+' | /usr/bin/grep -n "/Users/\|7656119\|@gmail"`
Expected: no output (the lines added name no home folder, SteamID or account).

- [ ] **Step 2: Run the full Swift suite**

Run: `swift test`
Expected: all pass.

- [ ] **Step 3: Run the milestone guest gates**

With `BUILD_DIR=~/Documents/MacProton/build`, run `install-scripts.sh`, `fonts.sh`, `game-exit.sh`, `user-shares.sh`
and `d3d-smoke.sh`.
Expected: each prints `ok`.

- [ ] **Step 4: Commit**

```bash
git add docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md docs/superpowers/plans/2026-10-24-macneutron-install-scripts.md
git commit -m "docs: D-16 and P1-4 follow the install-scripts spec" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 5: Hand the 3DMark check to the maintainer**

Ask before merging the branch into the main checkout's local `main`; the dev app and dev VM are built there. Then:
- `make app` in the main checkout. The maintainer opens the app once so it installs its launcher.
- Copy `eac-vm/guest/run-game.sh` to `build/eac-vm-work/run-game.sh`.
- Launch 3DMark Demo, by the maintainer or through the 3DMark loop exception.

Expected (spec §7):
- The first launch's launcher.log has `install script 231350:` lines for `x86 Update 4`, `x64 Update 4`, `dxsetup`
  and `FmSysInfo`, each `exit 0`, and none for `4.5.2`.
- 3DMark's "EasyFMSI not installed" error is gone.
- Steel Nomad: SUCCESS.
- The second launch has no `install script` lines.
