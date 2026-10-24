# A game's Steam install scripts run in its VM prefix

Status: approved 2026-10-24 (approach B; Proton's marks in run-game.sh's registry block). Plan:
`docs/superpowers/plans/2026-10-24-macneutron-install-scripts.md`.
Parent: `2026-10-15-macneutron-eac-vm-route-design.md` (every game runs in the VM since 0.6.0).
Amends: the parity spec's P1-4 (`2026-10-12-macneutron-proton-parity-design.md`) and the bridge spec's D-16
(`2026-09-28-macneutron-steam-bridge-design.md`, Amendment 2). Unblocks D-33's GamingRepair (§9).

## 1. Goal

Steam on Windows, and Linux Steam through Proton, runs a game's install scripts before its first launch. They install
redistributables ("Run Process") and write registry values ("Registry"). On MacNeutron nothing runs them, so a VM prefix
(`/home/mn/pfx` on `state/<appid>.img`) never gets them. Found 2026-10-24 with 3DMark Demo (231350): Futuremark
SystemInfo was never installed, because its script never ran.
- `~/Library/Application Support/Steam/registry.vdf` has no 231350 key.
- `~/Library/Logs/MacNeutron/launcher.log` has no `verb=run`: none in its 494 launches today, and none in the 8,783
  counted on 2026-10-04.
- Installing SystemInfo by hand in the dev VM's prefix worked (`msiexec /qn`). It needed `AppData\Local\Temp`, which
  is missing when the profile share isn't mounted.

The other games in this library have scripts that were also skipped. Their redistributables (VC++ 2012/2019/2022,
DirectX June 2010) were never installed in a VM prefix. Rogue Company's EAC setup, AoE2's URL installer and the
registry entries of 3DMark and AoE2 never ran either.

Done when:
- 3DMark Demo's first launch in the VM runs its scripts and the shared ones it uses, in its prefix, before the game.
  Its second launch runs none of them.
- The values Steam uses to remember a script ran (HasRunKey) are in the VM prefix's registry, where Proton's prefix
  keeps them.
- When Steam uninstalls a game, its VM drive is deleted (§4.9; added at the maintainer's request, 2026-10-24). A
  reinstall gets a new prefix and runs the scripts again, as with Proton.
- The gates in §7 pass.

## 2. What Steam does

**Which scripts.** Steam's record is the app manifest, not a file-name pattern: `steamapps/appmanifest_<appid>.acf`
has `InstallScripts` (depot → path relative to `installdir`) and `SharedDepots` (depot → the app that owns it, 228980
for the Steamworks Common Redistributables). The owner's own manifest maps those depots to their scripts. In this
library:

| App | `InstallScripts` | `SharedDepots` (228980's scripts) |
|---|---|---|
| 3DMark Demo 231350 | `installscript.vdf` (Registry), `installscript_si.vdf` (SystemInfo) | 228984 VC 2012, 228990 DirectX Jun2010, 229004 .NET 4.5.2 |
| AoE2 DE 813780 | `813780_install.vdf` (Firewall, Registry, URL installer) | 228988 VC 2019, 228989 VC 2022, 228990 |
| Palworld 1623730 | `installscript.vdf` (Firewall only) | 228989, 228990 |
| Rogue Company 872200 | `installscript.vdf` (UE4 prereqs, EAC setup) | 228988, 228990, 229002 .NET 4.0 |

The brief's `installscript*.vdf` pattern would miss AoE2's `813780_install.vdf`. The Steamworks documentation agrees:
Steam "will scan all of the mounted depots for that game for any file with the install script flag".

**What a script means.** These rules come from the Steamworks installscripts documentation
(partner.steamgames.com/doc/sdk/installscripts):
- For each "Run Process" entry, Steam looks up a DWORD under `HasRunKey` whose name is the entry's name. If the value
  is missing or 0 (or below `MinimumHasRunValue`), Steam runs `process 1` with `command 1`. It uses CreateProcess,
  then ShellExecuteEx if that fails.
- Exit code 0: Steam "writes your keyname as 1 in the registry". Any other code: Steam "will attempt to run it again
  on next launch".
- "Steam writes to the Wow6432Node path in the registry". HKCU\Software is the same in both views.
- `Requirement_OS` limits an entry to `Is64BitWindows` 1/0 or an `OSType` ("Windows XP" ... "Windows 10").
- Tokens: `%INSTALLDIR%`, `%ROOTDRIVE%`, `%STEAMPATH%`, `%APPDATA%`, `%LOCALAPPDATA%`, `%USER_MYDOCS%`,
  `%COMMON_MYDOCS%`, `%WinDir%`.
- Keys are case-insensitive (`hasrunkey`, `process 1`, `installscript` all appear). Entry names can repeat: .NET
  4.5.2 has two entries named "4.5.2", one limited to XP.

**Linux Steam with Proton.** Proton has no install-script code. Steam writes `legacycompat/evaluatorscript_<appid>.vdf`,
then runs `<tool> run <client>/legacycompat/iscriptevaluator.exe legacycompat\evaluatorscript_<appid>.vdf`, and Proton's
generic `run` starts it in the prefix (parity spec P1-4). The prefix comes with Proton's own marks: Valve's
`wine.inf.in` (proton_11.0), section `[ProtonOverrides]`, pulled in by `[BaseInstall]`, writes nine values. One of them:
`HKLM,Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\.NET\4.0,"4.0",0x10001,1`. The nine cover .NET 4.8, 4.7.2,
4.6.2, 4.5.2, 4.0, 4.0 Client Profile, 3.5, 3.5 Client Profile and XNA 4.0, so their installers never run. Valve's
wine.inf has no DLL overrides for what DXSETUP installs (d3dx9, d3dcompiler_43, xaudio2, xactengine).

**Mac Steam** has the same job. `steamclient.dylib` contains `CRunWindowsCompatInstallScriptJob`,
`%s/legacycompat/iscriptevaluator.exe`, `%s/legacycompat/evaluatorscript_%i.vdf` and
`standalone_installscript_progress_%u.vdf`. But Mac Steam ships no `legacycompat`, so the job ends at once and never
calls the tool's `run`. Valve's `bins_win64` package has no evaluator (parity spec), and nothing in the repo writes
Proton's nine marks.

## 3. Approaches

**A. Let Mac Steam drive it: put an evaluator at `<Steam>/legacycompat/iscriptevaluator.exe`.** This is closest to
Proton literally. Against it:
1. Valve's evaluator is a Linux-client binary from an unknown package, probably i386. It would have to be fetched and
   checked like `bins_win64`, and the P1-4 probe, which was never run, would have to show first that Steam then calls
   `run` at all.
2. Steam's `run` would boot the game's VM a second time, close to the game's own launch. run-guest.sh refuses a second
   VM on the same state disk (75). The evaluator's path isn't in a library either, so `MicroVM.guestPath` would send
   it to the macOS prefix. Both are fixable: queue the two boots on the disk's lock, route by app ID. But every first
   launch then boots twice.
3. MacNeutron.app would install a file in Steam's own folder and keep it there across Steam updates. That's the
   maintainer's call (question in §8).
4. The evaluatorscript and progress-file formats are Steam's and undocumented. We would rely on them without being able
   to test them without Steam.

**B (recommended). The launcher reads the scripts, and the game's own VM boot evaluates them before the game.**
- It uses Steam's record (the app manifests) and Steam's documented rules (§2).
- The HasRunKey values live where Proton keeps them: the prefix's registry, 32-bit view, with Proton's nine marks.
- One boot, no Valve binary, nothing written into Steam's folder, no probe.
- The cost is our own interpreter: about 150 lines of Swift with unit tests, and about 30 lines of run-game.sh.

**C. Valve's evaluator, driven by us.** It has A's download, plus we would have to write Steam's undocumented
evaluatorscript format ourselves. Rejected.

## 4. Design (B)

### 4.1 Where the launcher reads the scripts

New `Sources/MacNeutronCore/InstallScripts.swift`. `Launcher.runInMicroVM` calls it for `waitforexitandrun` only: Mac
Steam never sends an install-script `run`, and `run` stays Proton's generic verb. `MicroVM.script` puts the result in
front of run-game.sh, the same way the launch's other facts get there.
- `library` is the game's `.../steamapps/common` (from `MicroVM.guestPath`), and its parent is `steamapps`. The
  launcher reads `appmanifest_<appid>.acf`: `installdir`, `InstallScripts`, `SharedDepots`. For each owner app in
  `SharedDepots`, it reads `appmanifest_<owner>.acf` and keeps only the owner's `InstallScripts` for the depots the game
  lists.
- If a manifest or script is missing, or a shared app lives in another library, the launcher logs it and skips it.
- Order: the shared scripts first, in the game's `SharedDepots` order, then the game's own, in `InstallScripts` order.
  Dependencies go first. That's our choice: Steam's order isn't documented.
- Parsing uses `KeyValues.parse`. It keeps duplicates and keeps the file's order, and `node(at:)` looks keys up
  case-insensitively, as Steam matches them. The first root named InstallScript is the script; `kvsignatures` is
  ignored (§4.8).
- `parse` returns keys and values as written, still escaped: `HKEY_LOCAL_MACHINE\\SOFTWARE\\...`. Every key and value
  used below goes through `KeyValues.unescape` first, so `\\` becomes `\`.
- **"Run Process"** entries:
  - An entry runs only when its `Requirement_OS` holds: `Is64BitWindows` must be "1" (the VM prefix is 64-bit) and
    `OSType` must be "Windows 10" (what our Wine reports).
  - An entry with another requirement key, no `HasRunKey`, a `process 2`, or an unknown token is skipped, with a line
    in the log.
  - When several entries share a HasRunKey value, only the first that applies is kept. That's .NET 4.5.2's pair once
    the XP entry is dropped.
  - A `HasRunKey` under `HKEY_LOCAL_MACHINE\SOFTWARE\` maps to Wow6432Node, as Registry keys do (below). That's where
    Proton's marks are (§4.6).
  - `MinimumHasRunValue` defaults to 1.
  - `command 1` is split into arguments by Windows rules (whitespace separates, double quotes group).
- **"Registry"** entries:
  - A key under `HKEY_LOCAL_MACHINE\SOFTWARE\` becomes `...\Software\Wow6432Node\...`, because Steam writes the 32-bit
    view. Other HKLM keys and HKCU keys stay as they are.
  - `string` children become REG_SZ, `dword` children become REG_DWORD, and `(Default)` is the key's default value.
  - A language block (the documented `"string" { "<language>" { ... } }`) is skipped, with a line in the log.
- **Tokens:** case-insensitive. A token missing from this table is not guessed: the entry or value is skipped, with a
  line in the log.

| Token | In the VM prefix |
|---|---|
| `%INSTALLDIR%` | `Z:\mnt\games\<installdir of the script's app>`: the path the game itself runs from (run-game.sh's `EXE`; the VM prefix has no S: drive) |
| `%STEAMPATH%` | `C:\Program Files (x86)\Steam` |
| `%WinDir%` | `C:\windows` |
| `%APPDATA%`, `%LOCALAPPDATA%` | `C:\users\steamuser\AppData\Roaming`, `...\Local` |
| `%USER_MYDOCS%`, `%COMMON_MYDOCS%` | `C:\users\steamuser\Documents`, `C:\users\Public\Documents` |

`%ROOTDRIVE%` is left out: its format isn't documented.

### 4.2 What the guest gets

The launch script gets one more fact and one function. Every value is single-quoted with `MicroVM.quoted`, so nothing
from a script is ever parsed as shell. For 3DMark Demo:

```sh
MN_INSTALL_REG='REGEDIT4

[HKEY_LOCAL_MACHINE\Software\Wow6432Node\Futuremark\3DMark]
"Install_Path"="Z:\\mnt\\games\\3DMark Demo"
'
mn_install_script() {
  mn_run 'HKEY_LOCAL_MACHINE\Software\Wow6432Node\Valve\Steam\Apps\CommonRedist\vcredist\2012' 'x86 Update 4' 1 \
    '/mnt/games/Steamworks Shared/_CommonRedist/vcredist/2012' \
    'Z:\mnt\games\Steamworks Shared\_CommonRedist\vcredist\2012\vcredist_x86.exe' '/quiet' '/norestart'
  # ... x64 Update 4, dxsetup; .NET 4.5.2 (pre-marked by Proton's marks, §4.6, so mn_run skips it)
  mn_run 'HKEY_CURRENT_USER\SOFTWARE\Valve\Steam\Apps\231350' 'FmSysInfo' 1517 '/mnt/games/3DMark Demo' \
    'Z:\mnt\games\3DMark Demo\SystemInfoSetupAssistant.exe' 'Futuremark_SystemInfo_5_93_1517.msi'
}
```

The fourth argument is the process's folder as a Unix path, the working directory it runs in. The launcher knows it:
`Z:\X` is `/X`, and `C:\X` is `/home/mn/pfx/drive_c/X`. A game with no scripts gets neither the fact nor the function.

### 4.3 Where they run

In `eac-vm/guest/run-game.sh`, after the prefix, registry and font setup, and before `: > /mnt/share/traces/ready`.
The first-run toast ("Setting up <game>") stays up until the scripts are done. mn-runner runs the script as root, and
Wine runs as `USER=steamuser`, as for the game.
1. `mkdir -p` the profile's `AppData/Local/Temp`. msiexec needs it, and a profile folder made empty by
   `MicroVM.userShares` doesn't have it.
2. If `MN_INSTALL_REG` differs from the last text applied (kept in `/home/mn/pfx/.mn-installscript.reg`), import it with
   `wine reg import` and store it. A launch whose registry entries haven't changed pays nothing.
3. `mn_install_script`, if defined. `mn_run <key> <name> <minimum> <dir> <process> <args...>` does this:
   - It reads the current value from the prefix's `system.reg` (HKLM) or `user.reg` (HKCU) with awk. It returns if the
     value is ≥ `<minimum>`. When nothing is due, no Wine process starts.
   - In `<dir>` it runs `.exe` with `wine <process> <args>`, `.cmd`/`.bat` with `wine cmd /c <process> <args>`, and `.msi`
     with `wine msiexec /i <process> <args>`. That covers CreateProcess, and ShellExecuteEx for the rest.
   - It appends `<name> exit <status>` to `/mnt/share/traces/installscript.log`.
   - On exit 0 it runs `wine reg add <key> /v <name> /t REG_DWORD /d <minimum> /f`.
4. The game starts as now. run-game.sh's final `wineserver -w` covers installers that leave children behind.

The `run` branch of run-game.sh stays as it is (Proton's generic `run`).

### 4.4 Where HasRunKey is tracked

The values live in the VM prefix's own registry: `system.reg` and `user.reg` in `/home/mn/pfx`, on the game's state
disk. That's not the profile share, and not Steam's `registry.vdf`. They sit in the same key, view and value name that
Steam on Windows and Proton's prefix use. So:
- Proton's nine marks work unchanged (§4.6).
- A value that a game or installer writes itself is honoured.
- A new state disk is a new prefix, and everything runs again, as with a new Proton prefix.

Reading the .reg files is safe at that point. The marks were flushed when an earlier boot's wineserver exited.
Proton's marks are written by run-game.sh's registry block, which ends with `wineserver -k`. Within one run, each
`(key, name)` pair is checked once (§4.1 dedups).

The value written is `MinimumHasRunValue`, or 1 when the entry has none. The documentation says Steam writes 1 and is
silent on minimums. 3DMark's entry has a minimum of 1517:
- `SystemInfoSetupAssistant.exe` only runs `msiexec.exe /i <msi> /qn /L*V sii.log`.
- The MSI's string pool has no "Valve". A custom action inside its compressed cabinet could still write the value, and
  that grep wouldn't see it.

Writing the minimum is right either way. If the installer writes 1517 itself, our write is redundant. If it doesn't,
ours is the only thing that stops a reinstall at every launch. A minimum evidently exists so that a game can raise it
and make the step run again. The second-launch gate row (§7) checks this.

### 4.5 Failures and Steam's Stop

These follow Steam's rules:
- A non-zero exit leaves the value unwritten, so the step runs again at the next launch. The game still starts.
  Launcher.swift already exits 0 for a skipped installer for the same reason: a failed install script must not block
  the game.
- Steam's Stop during an installer ends the VM. The step is left unmarked, so the next launch runs it again.
- An installer that waits for a click would wait on the guest's screen (Xvfb), which nothing presents on the Mac. So
  each step gets `${MN_INSTALL_TIMEOUT:-900}` seconds. A timeout (status 124) is a failure: it's logged, nothing is
  marked, and the step is retried at the next launch. `wineserver -k` ends what the installer left running, and the
  game starts. This was added in the plan, and the maintainer approved it with the plan (2026-10-24).
- After the run, the launcher copies `traces/installscript.log` into launcher.log. A step that failed goes in as a
  summary line, so performance builds keep it. Successes go in as ordinary lines.
- No notification: Steam itself only retries.

### 4.6 Proton's marks

The nine `[ProtonOverrides]` values go into run-game.sh's existing one-time registry block, in the `.reg` it already
imports for the fonts. The block's condition gains one `grep` of `system.reg`. Existing prefixes, the dev VM's among
them, get the marks at their next launch.

Proton's own place for them is the guest Wine's `wine.inf`, via a patch in `eac-vm/wine-patches`. That needs a wine
stage and image rebuild. The block already stands in for Proton's wine.inf for `ShowCrashDialog` and the fonts. These
marks are required, not optional: without them, 3DMark's depot 229004 runs `Microsoft .NET Framework 4.5.2.cmd` in the
VM.

### 4.7 Files

- New: `Sources/MacNeutronCore/InstallScripts.swift` and `Tests/MacNeutronCoreTests/InstallScriptsTests.swift`.
- `MicroVM.swift`: `script(...)` takes the plan.
- `Launcher.swift`: the call, and the log lines after the run.
- `eac-vm/guest/run-game.sh`: Temp, the marks, `mn_run`, the call. run-game.sh comes from the VM folder, not the
  image, so no image rebuild is needed.
- One guest gate (§7).
- §4.9:
  - New: `Sources/MacNeutronCore/UninstalledDrives.swift`, with its tests.
  - `SteamLocation.swift`: the `apps` lists of `libraryfolders.vdf`.
  - `AppModel.swift`: the call after a refresh, and a `log` parameter so tests never write the real launcher.log.
- Docs: P1-4's status and D-16's text (§5).

### 4.8 Not built (each a line in the log when met)

- "Firewall": Wine has none. Under Proton, Valve's evaluator reaches Wine's firewall stubs, so the effect is the same.
- "Run Process On Uninstall", and the removal of values on uninstall or verify: these are Steam's uninstall. After
  an uninstall the whole VM drive goes (§4.9), and the values with it.
- `kvsignatures`: Valve's key isn't ours to check. Anyone who can change a script in the game folder can change the
  game's exe.
- Steam's progress UI. The first-run toast covers the first launch; a step that becomes due after an update just delays
  the game.
- Language blocks, `process 2+`, `%ROOTDRIVE%`, other `Requirement_OS` keys: until a game needs them.
- A game on macOS (`MACNEUTRON_VM=0`, or not in a library): no scripts. The i386 skip (Launcher.swift:113) stands, and
  its redistributables can't run there.

### 4.9 When Steam uninstalls a game, its VM drive goes

Linux Steam deletes `compatdata/<appid>`, the whole Proton prefix, when it uninstalls a game. Mac Steam deletes
nothing. MacNeutron already lists an uninstalled game's Mac prefix under "Leftover data" (`OrphanPrefixes`), for the
player to delete by hand, because it holds the profile and so the saves. Nothing touches the game's VM drive, which is
the rest of its prefix: Windows, Program Files, ProgramData, the registry, installed redistributables, its TPM and its
shader cache. That drive now goes on its own, as Proton's prefix does.

- **Who and when.** MacNeutron.app, whenever it re-reads Steam's files: every minute, and when Steam starts or quits.
  It runs off the main thread, after the snapshot (`AppModel.refresh`). The launcher only runs during launches, so it
  can't notice an uninstall.
- **What counts as uninstalled.** A drive `state/<id>.img` whose `<id>` is a Steam app ID (1 to 0x7fffffff) that meets
  both conditions:
  - It's in no library's `apps` list in `steamapps/libraryfolders.vdf`.
  - It has no `appmanifest_<id>.acf` in any library.

  `libraryfolders.vdf` keeps listing a game whose library is on a disconnected drive, so that game's drive stays.
  When the file can't be read or parsed (Steam rewriting it), nothing is deleted that round. Drives with other names
  are never touched: the dev VM's test drives (`fonts.img`), and `0.img`.
- **A running VM is never touched.** The deletion holds the game's run lock `runs/<id>.lock` (`open` with
  `O_EXLOCK | O_NONBLOCK`), the lock run-guest.sh takes with `lockf`. If it's held, the drive waits for the next round.
  While the deletion runs, a launch of that game is refused, as a second VM is.
- **What goes:** `state/<id>.img`, `state/<id>.tpm/`, `state/<id>.dxmt/`, `runs/<id>/` and `runs/<id>.lock`.
- **What stays:**
  - The Mac prefix `compatdata/<id>`, which is Mac Steam's and holds the saves: it's still in "Leftover data".
  - The TPM's 32-byte key in the login keychain. It's macneutron-vm's item, and deleting it from the app could raise a
    keychain prompt. The code will carry a `ponytail:` note.
- **Told:** one summary line per drive in launcher.log, `deleted the game VM drive of app <id>: Steam uninstalled it
  (<size>)`. There's no notification.
- **What's lost:** what a game kept outside its profile, such as saves in its registry, ProgramData or
  `C:\users\Public`. That's the same as Proton deleting its prefix. Steam Cloud and the profile on the Mac aren't
  touched.

## 5. Proton parity

What matches Linux Steam with Proton:
- The same scripts (Steam's record).
- The same prefix, before the game.
- The same registry key, view and value name for HasRunKey.
- The same run, mark and retry rules (Steamworks documentation).
- Proton's nine .NET/XNA marks.

What differs, each for a macOS reason:
- **The evaluator is ours, not Valve's `iscriptevaluator.exe`.** Mac Steam ships no `legacycompat`, so it never calls
  `run`. Valve's evaluator would be a Linux-client binary to fetch and keep in Steam's folder.
- **The scripts run in the game's own VM boot, not in a `run` before it.** A second VM on the same state disk is
  refused. One boot also saves a first launch the cost of a second.
- **No signature check, no progress UI** (§4.8).
- **Uninstall deletes the VM drive but not the Mac prefix** (§4.9). The VM drive is the part of the prefix MacNeutron
  owns, and it goes as Proton's prefix does. The Mac prefix is Mac Steam's, which deletes nothing; it stays in
  "Leftover data" with its warning about saves.

New D-16 text for the bridge spec's Amendment 2:

> **D-16:** Install scripts are run by MacNeutron, not by Steam. Mac Steam has the job (`steamclient.dylib`:
> `CRunWindowsCompatInstallScriptJob`, `legacycompat/iscriptevaluator.exe`) but ships no `legacycompat`, so it never
> calls the tool's `run`. For a game in the VM, the launcher reads the scripts Steam's app manifests list, and
> run-game.sh evaluates them in the game's prefix before the game. It follows Steam's documented rules: a HasRunKey
> DWORD in the 32-bit view, exit 0 marks the step, anything else retries. It also writes Proton's `[ProtonOverrides]`
> CommonRedist marks (spec `2026-10-24-macneutron-install-scripts-design.md`). A game on macOS still runs none: its
> i386 installers can't run there.

P1-4's steps 1 (probe) and 3 (Valve's evaluator) are replaced by this. Step 2 (the CommonRedist keys) is §4.6, in the
VM prefix. Step 4 (a notification for the i386 skip) is dropped: macOS runs no scripts.

## 6. Risks

- **Redistributables under FEX and Wine.** A failing one retries at every launch, as Steam does, and costs that time
  again. The log shows it.
- **DXSETUP installs native d3dx9, d3dcompiler_43, XAudio2 and XACT into system32 and syswow64.** Proton does the same,
  and Valve's wine.inf has no overrides for them. 3DMark (228990) checks Steel Nomad after it.
- **The exit status is `wine`'s, the Windows code mod 256.** A failure code whose low byte is 0 would mark the step.
  It is rare, and getting the full code would need a Windows-side wrapper. The code will carry a `ponytail:` note.
- **First launches get longer.** 3DMark runs VC 2012 twice, DXSETUP and SystemInfo under FEX. This is unmeasured, and
  Linux Steam pays the same.
- **Our choices where Valve's evaluator is undocumented:** the order (§4.1), the working directory (the process's
  folder: 3DMark's `sii.log` lands in its game folder, which is writable in the VM; Steamworks Shared is read-only),
  and the value written for minimums (§4.4).

## 7. Testing (lighter-task rule: one lane per task)

- **Swift, `InstallScriptsTests`:** a fixture library in a temp folder, with no Steam and no VM. It covers:
  - 3DMark's layout gives §4.2's plan: shared first, the XP entry dropped, `Is64BitWindows` kept,
    `Z:\mnt\games\...` paths, Wow6432Node.
  - Key case variants.
  - Duplicate names.
  - An unknown token, section or requirement, skipped with a log line.
  - A missing shared manifest or script, skipped.
  - A value with `'` quoted safely.
  - One `LauncherTests` row: a VM `waitforexitandrun` script has `mn_install_script`, and `run`'s doesn't. It runs with
    a temp `MACNEUTRON_VM_DIR`.
- **Guest gate, `eac-vm/tests/install-scripts.sh`:** a fake game whose scripts run test programs, built with
  `eac-vm/guest/build-win-tests.sh`. There are two boots.
  - An .exe that exits 0 is marked. One that exits 1 is not, and runs again on the second boot.
  - A .cmd in a path with spaces, copied from Valve's VC 2022 one (`start /w "" "%~dp0\<exe>" ...`, 3010 → 0, then
    `EXIT /B %ERRORLEVEL%`), around a test exe exiting 3010 (marked) and 1 (not marked). This proves Wine's
    `cmd`/`start /w` passes the child's code through, which the retry rule depends on.
  - `MinimumHasRunValue 5` writes 5, and the step is skipped on the second boot.
  - A .NET 4.0 entry is skipped thanks to Proton's marks.
  - A Registry entry lands in Wow6432Node, and the second boot doesn't import it again.
- **Swift, `UninstalledDrivesTests` and one `AppModelTests` row** (§4.9), with temp folders only. The tests check that:
  - An uninstalled app's drive and its folders go.
  - An installed app's drive stays.
  - The drive of an app listed only in `libraryfolders.vdf` (its drive disconnected) stays.
  - Nothing goes when `libraryfolders.vdf` is unreadable.
  - Drives with other names stay, and so does `0.img`.
  - A drive whose run lock is held stays.
  - A refresh of the app deletes an uninstalled game's drive and logs it.
- **Maintainer:** 3DMark Demo, launched from Steam.
  - First launch: launcher.log shows VC 2012 x86 and x64, dxsetup and FmSysInfo exiting 0. On the dev prefix,
    SystemInfo runs once more, because nothing set FmSysInfo.
  - 3DMark finds SystemInfo: the "EasyFMSI not installed" error is gone. SystemInfo's scan may still fail in the VM, as
    it did after the hand install.
  - Steel Nomad: SUCCESS.
  - Second launch: no install-script lines.

## 8. Questions for the maintainer

1. **Approach B** (§3)? Or first run P1-4's probe, to see whether Mac Steam would call `run` for approach A? That
   would also mean MacNeutron.app keeping a file in `~/Library/Application Support/Steam/legacycompat`.
2. **Proton's marks** (§4.6): the proposed default is run-game.sh's registry block. It stands unless you prefer
   Proton's own place, a guest `wine.inf` patch, which needs an image rebuild.

## 9. Follow-ups (not in this design)

- **GamingRepair (D-33).** Proton's steam.c opens `HKLM\Software\Valve\Steam\Apps\<appid>` in the 32-bit view, a key
  only install scripts create. AoE2's HasRunKey (`...\Apps\813780\URLInstaller\4`) will create it in the VM prefix, so
  that port stops being a no-op.
- Install scripts for a game on macOS, if the Mac runtime ever runs i386.
