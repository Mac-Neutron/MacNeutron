# MacNeutron: Proton 11 parity for the Steam connection

Status: approved by the maintainer (2026-10-12). The maintainer's rule: MacNeutron's Steam connection follows Proton 11's
way of doing things, and deviates only for a documented macOS reason. This spec is the parity audit's plan (part B),
with the adversarial review's corrections (part A), which bind where they differ.

## Part A. Review corrections (binding)

Corrected P0, in order:
0. **The tool-name regression (§0).** The Steam session started at 21:44 on 2026-10-11 registered only the legacy
   `macneutron` tool and mapped every Windows game to it (config.vdf: 73 apps to `macneutron`, 0 to
   `macneutron-proton`). Cause: the `build/palfix` test app (the spike app with a new runtime) carries a launcher from
   before the rename, and opening it rewrote the tool files and mappings. Fix: a full dev app built from `main`, which
   re-registers `macneutron-proton` and migrates the mappings; check the counts afterwards. This blocks Steam Cloud and
   P1-5.
1. **P0-1, Valve's signed steamclient64.dll by default:** the approved spec
   (docs/superpowers/specs/2026-10-12-macneutron-valve-steamclient-design.md), plus extraction (A2) and the release guard
   (A7, extended with bin/SteamService.dll, gameoverlayui*, SteamOverlayVulkanLayer*). No overlay or SteamService
   deployment in P0.
2. **P0-3, the S: game drive, gated:**
   - When validation fails, follow Proton's fallback (S: is the steamapps folder itself), or record "no S:" as a
     deviation for the maintainer to decide.
   - Honour `PROTON_SET_GAME_DRIVE=0` as the per-game opt-out.
   - The three-appid helper-path list (311210, 1985810, 1549250 run `C:\Program Files (x86)\Steam\steam.exe`) comes with
     P1-1's `run_process` port.
   - Gate: a maintainer smoke on Palworld, SMITE 2 and one launcher-based game.
   - Without those, it is P1.
3. **lsteamclient's bad free** (LSC/unixlib.cpp:949-959) moves to P1, as a cherry-pick of `f46fd902` and `b94341c4` from
   Proton's `bleeding-edge`, riding along with the next lsteamclient rebuild.

Other corrections:
- **P1-3 overlay (H1).** dyld applies `__interpose` tuples only for images present at launch. A `dlopen`ed
  gameoverlayrenderer.dylib would lose cursor release, mouse capture, the run-loop pump and the GL hooks. The spike's
  test dylib carries an `__interpose` tuple (e.g. CGWarpMouseCursorPosition) and checks whether it fires after
  `dlopen`. The second option to evaluate is adding `com.apple.security.cs.allow-dyld-environment-variables` to
  wine.app, a signing decision on our own app, not Reduced Security. The launcher would then set `DYLD_INSERT_LIBRARIES`
  to gameoverlayrenderer only, with Proton's explorer strip ported. GameOverlayRenderer64.dll is decoupled from the
  spike: it has no tier0/vstdlib imports, and 0036's name match suffices.
- **P1-2 locale (M4, M6).** `steamclient_init_registry` returns void and can't report failure, so write today's keys
  first, then call init (it overwrites them on success). Steps 1 and 2 are one change. Dropping `LC_ALL` leaves unix
  libraries in the C locale on macOS, because Mac Steam sends no `LANG`: add a non-ASCII home or library path test
  first.
- **P1-5 STEAM_COMPAT_PROTON (M5).** Run the Palworld and SMITE 2 A/B only after §0 is fixed, and change the default
  only after it passes.
- **P1-1 restart (L6).** The test's child exits around the signal (Proton restarts only after the child exits).
- **P1-1 GamingRepair (L5)** waits for P1-4: it only opens a key that install scripts create.
- **P1-4 probe stub (L4).** It exits 0 and writes a marker file, so Steam doesn't record a failed install script.
- **ActiveUser (L2).** Proton never writes it. Drop it from bridge/steam.c and drop the launcher's loginusers.vdf read,
  which is also an account-data read.
- **steamclient_init_registry with no steamclient.dylib (L3).** Add a bridge/check.sh case with a missing client
  folder; optionally add a NULL guard in lsteamclient.
- **D-13a ValvePlatformMutex (L7).** Port Proton's value (GetModuleFileNameW), or restate the reason (letter case).
- **D-20 crash reports (L8).** Mac Steam sets `PROTON_CRASH_REPORT_DIR`: call it "not ported yet", not a macOS reason.
- **Wording.** Proton copies the legacycompat files only if they exist (A5, L9).
- **Tool-link move (L10).** Moving the tool links (P2-1) must rewrite the app spec's steam_dev.cfg invariant (both tools
  under B/compatibilitytools.d). A `DYLD_INSERT_LIBRARIES` passthrough needs a Timberborn before-and-after test.
- **Licence.** Fine. No Valve binary is in git or shipped; the A7 guard must exist before any deployment change.

## Part B. The audit's plan

# MacNeutron's Steam connection: Proton 11 parity plan

Research only; nothing was changed or launched. The rule applied throughout: MacNeutron does what Proton 11 does, unless there is a documented macOS reason not to. A copy of this plan is at `/private/tmp/claude-501/proton-parity/design-lead/plan.md`.

**Citation key**
- **Ours** (repo-relative):
  - `MNC/` = `Sources/MacNeutronCore/`
  - `W/` = `build/wine-arm64-src/wine/` (Wine 11.19 plus `wine-arm64/patches/wine/0001-0036`)
  - `LSC/` = `build/wine-arm64-src/lsteamclient/lsteamclient/`
- **Proton:**
  - `proton.py` is Proton 11's `proton` script.
  - `steam.c` is `steam_helper/steam.c`.
  - `loader.c` and `unix-loader.c` are Proton Wine's ntdll.
  - All four are in the proton-steam scratchpad.
- **Valve and Proton trees:**
  - `VW:` = ValveSoftware/wine `proton_11.0`.
  - `PG:` = proton-git `origin/proton_11.0` (commits and files).
- **Steam logs:** `compat_log*.txt` and `console_log.txt` are in `~/Library/Application Support/Steam/logs/`. Game logs are `~/Library/Logs/MacNeutron/steam-<appid>.log`.

**What I verified myself (load-bearing claims):**
- **Valve's `bins_win64` zip** (the 2026-10-11 download in the scratchpad; SHA-256 matches manifest version 1788652215, `sha2 93f5b6…`):
  - It holds every file Proton copies from `legacycompat`. `steamclient64.dll` is x64. `GameOverlayRenderer64.dll` is x64. `steamclient.dll`, `Steam.dll`, `GameOverlayRenderer.dll` and `bin/SteamService.exe` are all i386; SteamService.exe is a console program.
  - It does **not** hold `iscriptevaluator.exe` (51 files listed).
- **Proton's Steam-file copy and overrides:** proton.py:1116-1124 copies those files, with SteamService.exe renamed to `steam.exe`. proton.py:1515 sets `steam.exe=b`.
- **S: game drive:** proton.py sets it up during prefix setup at :1259 and before `run` at :2162. It is on by default (:1457). `PG` history shows about 20 games enabled one at a time before d93f8a66 and fa493fa6 (2026-08-04) made it the default.
- **Proton's steam.exe** does more than ours: steam.c:92-111, 206-221, 299-748 and 931-1040, against our `bridge/steam.c` (269 lines).
- **lsteamclient bug:** LSC/unixlib.cpp:949-959 frees `buffer` without initialising it, and `ntdll_get_unix_file_name` returns early without setting it (W/dlls/ntdll/unix/file.c:4532-4534).
- **Hardened wine.app:** `wine-arm64/wine.entitlements` has no `allow-dyld-environment-variables`, but it does have `disable-library-validation`.
- **Steam passes `LC_ALL=en_US.UTF-8`,** and Wine on macOS falls back to CoreFoundation only when the env locale doesn't resolve (W/dlls/ntdll/unix/env.c:638-649, 735-793).
- **Mac `steamclient.dylib` strings:**
  - Present: `%s/legacycompat/iscriptevaluator.exe`, `STEAM_COMPAT_PROTON(_SUPPRESS)`, `STEAM_COMPAT_CONFIG`, `HOST_LC_ALL` and `compatmanager_layer_name`.
  - Absent: `destroyprefix`.
- **Tool registration:**
  - Mac Steam registered a tool from `~/Library/Application Support/Steam/compatibilitytools.d` and launched through it, which shows Linux mode was on (compat_log.before-acceptance.txt:4-6, 123).
  - Our links live in the Steam bundle instead (MNC/SteamLocation.swift:24, MNC/SteamPlayMode.swift:42, 246-254).
- **Tool-name counts** in Steam's `config.vdf` (§0); only the counts were read.
- **Image machine type:** Wine takes a new process's machine from the image's PE header (W/dlls/ntdll/unix/process.c:262-292). An i386 file at a path we execute therefore can't run without WoW64.

---

## 0. Check this before any other work

**Finding: the Steam Cloud rename is not in effect.**
- The Steam session running since 21:44 registered only the legacy `macneutron` and `macneutron-native` (compat_log.txt:13810, 13812). The sessions at 21:10 and 21:15 registered `macneutron-proton` (compat_log.txt:13075, 13441).
- Steam's `config/config.vdf` (written 22:53) maps 73 apps to `"macneutron"`, 48 to `"macneutron-native"` and **0** to `"macneutron-proton"`. Only those name counts were read, nothing else from the file.

Two things key on the "proton" name, so both are off today:
- Mac Steam maps Auto-Cloud's Windows roots into the prefix (cloud spec §2).
- Mac Steam sets `STEAM_COMPAT_PROTON` (P1-5).

**Maintainer:** find what put the legacy name back in both the tool vdf and the mappings, for example an older MacNeutron.app running `RuntimeInstaller.writeToolFiles` and `applyMappings` around 21:44. Read the installed `MacNeutron/compatibilitytools.d/macneutron/compatibilitytool.vdf`; it is off-limits to this research. Then redo the migration before more Cloud or P1-5 work.

---

## 1. Summary

### Where we already match Proton

- **Registration and verbs:**
  - The `compatibilitytool.vdf` and `toolmanifest.vdf` v2 shape (MNC/RuntimeInstaller.swift:35-51, 64-72).
  - The mapping format and priorities: 250 per app, 75 global (MNC/MappingPlanner.swift:29-34, 76-82).
  - `waitforexitandrun` order: prepare, `wineserver -w`, run (MNC/Launcher.swift:89-119 = proton.py:2165-2169).
  - `run`, `runinprefix`, `getcompatpath` and `getnativepath` (Launcher.swift:83-88, 120-123 = proton.py:2160-2181).
  - Exit 1 without `STEAM_COMPAT_DATA_PATH` or a verb.
  - Steam's working directory is kept.
- **Prefix:**
  - `compatdata/<id>/pfx`, with a lock held for preparation only (MNC/CompatContext.swift:26-28 = proton.py:641, 989).
  - Steam-seeded prefixes are built in place.
  - The XP-name profile links Steam Cloud needs (MNC/ProfileLinks.swift). Ours are safer than Proton's merge.
- **lsteamclient:** the tree is byte-identical to `proton_11.0` (pin `wine-arm64/deps.pins:23`, `db9e6ff`; tree `1105024f`) plus three macOS patches. Patch 0036 does Proton's genuine-steamclient64 trampolines (W/dlls/ntdll/loader.c:2279-2410 = loader.c:2377-2420), including `LDR_DONT_CALL_DLLMAIN`, the tier0/vstdlib redirect and `PROTON_DISABLE_LSTEAMCLIENT`.
- **steam.exe** (`bridge/steam.c`) already follows steam.c on:
  - the `ActiveProcess` pid and client DLL values;
  - `SteamPath` and `ValvePlatformMutex`;
  - `Steam3Master_SharedMemLock` and `Valve_SteamIPC_Class`;
  - the SteamStub DIPC answer;
  - becoming a Wine system process and waiting for Wine's shutdown event;
  - `PROTON_HIDE_PROCESS_WINDOW`;
  - the `Apps\<id>` key and language.

  The launcher writes `Steam\config` and `steamapps\libraryfolders.vdf` at every launch (MNC/PrefixManager.swift:224-232).

### Gaps that matter, by player impact

1. **Valve's signed steamclient64.dll is not deployed by default.** The spec is approved but not built, so SteamStub games such as Palworld fail without a launch option.
2. **No S: game drive.** Games see `Z:\Users\…\Library\Application Support\Steam\…`. Proton turned the drive on by default after about 20 per-game breakages.
3. **A bad `free()` in lsteamclient** can abort the game when a path conversion fails.
4. **steam.exe is a partial port.**
   - It never uses ShellExecute, so URL and document launch targets fail, link2ea:// among them.
   - It has no command handler and no in-place restart.
   - It doesn't call `steamclient_init_registry`, so the game's Steam language never reaches the locale.
   - Steam's `LC_ALL=en_US.UTF-8` reaches every game, so every game sees en-US.
5. **No Steam overlay:** no Shift+Tab, purchases, F12 or toasts.
6. **Install scripts never run** on Mac Steam (no `legacycompat`), so redistributables and install-script registry keys never land.
7. **`STEAM_COMPAT_PROTON` is stripped,** with no macOS reason.

### Deliberate macOS deviations (each documented in part 4)

- **Launching the tool:**
  - The tool entry point is a Mach-O, not Python.
  - There's no Steam Linux Runtime (no `require_tool_appid`).
  - Steam Play mode depends on the steam_dev.cfg gate, plus the native tool for Mac games.
- **Process lifetime:** a second `wineserver -w` and SIGTERM → `wineserver -k`, because Mac Steam has no subreaper.
- **Runtime:** msync instead of fsync, and DXMT instead of DXVK and vkd3d.
- **Steam client and bridge:**
  - The 0036 trampolines are written PE-side, because Apple Silicon refuses W^X violations.
  - lsteamclient's optional exports, and keysyms that return 0.
  - `steam://` goes through `open(1)`.
  - The client path is the Steam bundle's MacOS folder.
  - The lsteamclient fallback when Valve's DLL isn't cached.
  - No i386 Valve files until WoW64.
- **Logs and launch options:** logs live in `~/Library/Logs`, and launch options need `/usr/bin/env`.

### Deviations that fail the macOS-reason test: revisit, don't document as-is

| Deviation | Where | Decision needed |
|---|---|---|
| `STEAM_COMPAT_PROTON` removed | MNC/LaunchEnvironment.swift:10-12; cloud spec D5 | Pass it through (P1-5) |
| Profile `users/$USER`, not `steamuser` | Wine default; cloud spec §2, §4.2 | `steamuser` for new prefixes (P2-9) |
| Wine links Desktop, Documents and the other profile folders to the Mac's (`~/Documents/My Games` saves) | Wine default; cloud spec D6 | Valve's "never link home dirs" for new prefixes (P2-9) |
| Tool links in the self-updating Steam bundle | app-design spec §1 "Approach B" | Proton location `<Steam root>/compatibilitytools.d` (P2-1) |
| Valve spec §6 defers `GameOverlayRenderer64.dll`, `SteamService.exe` and `Steam.dll` "until a game needs them" | valve-steamclient spec line 94 | Replace with the runtime conditions in part 3 |
| `steam.exe` run from `Program Files (x86)\Steam`, not `system32` | MNC/SteamBridge.swift:7 | P1-1 |
| steam.exe writes `ActiveProcess\ActiveUser` from `loginusers.vdf`. Neither Proton's steam.exe nor `steamclient_init_registry` writes it | bridge/steam.c:104-105; Launcher.swift:192-196 | Record the open bridge-spec §10 answer (does `SteamAPI_Init` need it?). If not, drop the write and the `loginusers.vdf` read |
| Outside the Steam connection: trimmed WINEDEBUG channels; prefixes full of copies instead of symlinks (about 1.2 GB each) | LaunchEnvironment.swift:18-24; PrefixManager.swift:110 | Runtime-parity plan |

---

## 2. Prioritized gaps

Test tools available without Steam:
- Swift unit tests: `Tests/MacNeutronCoreTests`.
- `bridge/check.sh` (`make bridge-check`): real wine.app, no Steam, a helper program, and a stub `open(1)`.
- `wine-arm64/check.sh steamclient`: 0036, run against Valve's DLL.

### P0: breaks or likely breaks real games

**P0-1 Valve's signed steamclient64.dll by default** (build the approved spec, with the part 3 amendments)
- **Proton:** copies `legacycompat/steamclient64.dll` into `Steam\` (proton.py:1116-1124). The loader redirects its exports to lsteamclient (loader.c:2377-2420; unix-loader.c:1299-1375).
- **Ours:**
  - lsteamclient.dll is copied under that name, and Valve's DLL is used only with `MACNEUTRON_STEAMCLIENT64=<path>` (MNC/SteamBridge.swift:17-25; PrefixManager.swift:202-219).
  - Patch 0036 is done (`wine-arm64/patches/wine/0036-…patch:148-170`).
- **Change:** valve-steamclient spec §4:
  - `ValveClient` (fetch, verify, extract, cache);
  - the `genuine-steamclient` runtime marker (`wine-arm64/build.sh`, `ToolLayout.supportsGenuineSteamclient`);
  - `deploySteamBridge` order: override, then cache, then lsteamclient;
  - the app's download, Settings row and `release.sh` guard.

  Fold in part 3 now, so the cache holds every legacycompat file from the first download.
- **Test, no Steam:** the spec §5 Swift rows (fake manifest and zip with tiny PEs), plus `wine-arm64/check.sh steamclient` with the cache as its DLL source.
- **Maintainer:**
  - Palworld starts with no launch option, and launcher.log names the cached DLL.
  - SMITE 2 still runs.
  - Settings → Remove puts lsteamclient back.
- **Cost:** L.
- **Dependencies:** none. P1-3 (overlay DLL) and the WoW64-era files depend on it.

**P0-2 lsteamclient: cherry-pick the two upstream fixes**
- **Proton:** `bleeding-edge` commits `f46fd902` (sets `buffer = NULL` in `get_unix_file_name`) and `b94341c4` (`url_unescape` decodes UTF-8). Neither is on `proton_11.0` yet.
- **Ours:**
  - LSC/unixlib.cpp:949-959 frees an uninitialised pointer when `get_nt_path` fails (W/dlls/ntdll/unix/file.c:4532-4534). Depending on the stale stack value, that is a no-op or an abort of the game.
  - LSC/unixlib.cpp:973 garbles non-ASCII `file://` URLs.
- **Change:** add `wine-arm64/patches/lsteamclient/0004` and `0005`. The lsteamclient layer reports that both pass `git apply --check` on our tree. Drop them when `proton_11.0` carries them. At each Proton point release, re-run `git diff <pin> origin/proton_11.0 -- lsteamclient`.
- **Test:** `wine-arm64/build.sh` applies the series, then `wine-arm64/check.sh steamclient`.
- **Maintainer:** none beyond the usual Palworld and SMITE 2 smoke.
- **Cost:** S.
- **Games affected:** any whose Steam API call fails a path conversion: UGC/Workshop, RemoteStorage, Screenshots, `SetInputActionManifestFilePath`, HTMLSurface. The second fix covers non-ASCII home folders.

**P0-3 S: game drive, with Proton's path conversion** (one entry for the prefix, verbs-env and wine-steam rows)
- **Proton:**
  - `gamedrive` is on by default (proton.py:1457).
  - `dosdevices/s:` points to the parent of the `steamapps` folder that contains the game, checked by `get_validated_steamapps_parent`: basename is `steamapps`, parent writable, same device (proton.py:299-321, 333-348). It is applied in `setup_prefix` (:1259) and before `run` (:2162).
  - steam.exe receives the raw Unix path and converts it with `wine_get_dos_file_name` (steam.c:443-520). Wine picks the deepest matching drive (W/dlls/ntdll/unix/file.c:4229 `find_drive_nt_root`), so the game sees `S:\steamapps\common\…`.
  - `libraryfolders.vdf` goes through the same conversion (steam.c:750-816).
  - History: per-game enables for Oblivion Remastered (PG a974018d), Once Human (77aa1be5), The First Berserker: Khazan (707c83d5), Avatar: Frontiers of Pandora (8620b90a), Tales of Berseria (9e7fff2d), Sea Power (bf6e654a), Assassin's Creed Shadows (d811fe45), Dragon's Dogma: Dark Arisen (ee81b722) and others. Then the parent-folder fix (d93f8a66) and removal of the per-game workarounds (fa493fa6).
- **Ours:**
  - The launcher pre-converts the path to `Z:` (MNC/SteamBridge.swift:10-12, Launcher.swift:155).
  - `libraryfolders.vdf` uses `Z:` (SteamBridge.swift:32-41).
  - There is no `s:`.
- **Change:**
  1. In `PrefixManager.prepare`, under the lock, run Proton's `setup_dir_drive` for `s:`. Pick the `STEAM_COMPAT_LIBRARY_PATHS` entry that contains `STEAM_COMPAT_INSTALL_PATH`, matching by path component (Proton's substring test can match a prefix of a longer folder name). Validate it as proton.py:299-309 does. Create, retarget or remove the link.
  2. The launcher passes the raw Unix target to steam.exe: drop `windowsPath` from Launcher.swift:155. A direct start (no bridge) already gets Wine's own conversion.
  3. Port the Unix-path branch of steam.c's `run_process` (443-520) into `bridge/steam.c`:
     - quote handling;
     - `wine_get_dos_file_name` via `GetProcAddress(kernel32)` (W/dlls/kernel32/kernel32.spec:1740);
     - `SHGetFileInfoW(SHGFI_EXETYPE)` giving `CREATE_NEW_CONSOLE` for console programs.
  4. Move `setup_steam_files` into `bridge/steam.c` (steam.c:750-816), leaving out entry 0 (deviation D-8 in part 4). Delete `PrefixManager.writeSteamFiles` and `SteamBridge.libraryFolders`, and their tests.
- **Test, no Steam:**
  - Swift `PrefixManager`: the link is made to the library's parent. It is not made when the basename isn't `steamapps`, the device differs, or the parent isn't writable. It is retargeted when the target changes, and removed when no library matches.
  - Swift `Launcher`: the steam.exe command line carries the raw Unix path.
  - `bridge/check.sh` with a fake library at `<work>/Lib Ünï/steamapps/common/Game`, `STEAM_COMPAT_LIBRARY_PATHS` and `STEAM_COMPAT_INSTALL_PATH` set:
    - the helper's `GetModuleFileNameW` is `S:\steamapps\common\Game\helper.exe`;
    - arguments are unchanged;
    - `libraryfolders.vdf` lists `S:\`;
    - a console helper gets its own console.
- **Maintainer:** Palworld and SMITE 2 start, and the game log shows `S:\steamapps\…`. Saves still load: they live in the profile, which this doesn't touch.
- **Cost:** M.
- **Dependencies:** shares the `run_process` port with P1-1, so land them together.

### P1: player-visible features (and probes that decide P0-class work)

**P1-1 Complete the steam.exe port: system32 launch, ShellExecute, command handler**
- **Proton:**
  - The helper is `C:\windows\system32\steam.exe` (proton.py:2117-2122), built as a Wine builtin (PG Makefile.in:462-470). `steam.exe=b` (proton.py:1515; Locoland 352130 exempt, :1523-1525) makes every run of any `steam.exe` reach it, including `Steam\steam.exe`, which is Valve's SteamService.exe (:1119).
  - `run_process` starts any target that isn't a `.exe` through `ShellExecuteW`. That covers `link2ea://` and `steam2ea://` with EA Desktop recovery (steam.c:299-362, 523-630; PG 5b89db94) and Source SDK's `-game` path fix-up (364-415, PG a8b51747).
  - `steam_command_handler` (652-748):
    - `-- steam://rungameid/<SteamGameId>` or `steam://launch/<id>` restarts the game in place through the `PROTON_STEAM_EXE_RESTART_APP` event (673-697, 1013-1040).
    - Other `steam://` URLs and `-` options are forwarded to the native client.
  - Game setup happens only when `SteamGameId` is set (931).
  - A dummy `vguiPopupWindow` window titled "Steam" is created, after `GetDesktopWindow` on the main thread (92-111, 942-944; PG 42f903ef).
  - The `GamingRepair` mark (75-90, 947) and `PROTON_WAIT_ATTACH` (963).
  - `HKCR\steam` → `Steam\Steam.exe -- "%1"` (VW wine.inf.in:316).
- **Ours:**
  - The helper is run at `Steam\steam.exe` (SteamBridge.swift:7, Launcher.swift:155).
  - It always uses `CreateProcessW` (bridge/steam.c:240), so a URL or document target exits 1.
  - There is no command handler: a game running `steam.exe -applaunch …` or `-- steam://…` makes our helper try to start "-…" as a program.
  - It writes everything whether or not `SteamGameId` is set.
  - `HKCR\steam` → winebrowser → `open(1)` (bridge/steam.c:113-124). A game restarting itself through Steam reaches Mac Steam while it still shows the game as running.
- **Change:**
  1. `PrefixManager` also installs `bin/steam.exe` as `drive_c/windows/system32/steam.exe`, and `SteamBridge.steamExe` becomes `C:\windows\system32\steam.exe` (Proton's argv). The copy at `Steam\steam.exe` stays for now (part 3 A4 says when it changes), and `removeSteamBridge` (PrefixManager.swift:237-243) gains the `system32` copy.
  2. Port `should_use_shell_execute`, the ShellExecute branch with the EA workarounds, and `fixup_cmdline_paths`. All of it is plain Win32.
  3. Port `steam_command_handler` with the restart event. Strip a leading `--` (the `HKCR` form), then forward `steam://` URLs to Mac Steam through `open(1)`, with the URL as its only argument: `__wine_unix_spawnvp` (W/dlls/ntdll/ntdll.spec:1770) with `/usr/bin/open`, or the existing winebrowser route. Map `-applaunch <id>` to `steam://rungameid/<id>`. Log and ignore other `-` options, because Mac Steam has no CLI to forward them to (deviation D-12). Set the `HKCR\steam` command to Proton's value, `"C:\Program Files (x86)\Steam\steam.exe" -- "%1"`, which reaches our helper copy.
  4. Gate the game setup on `SteamGameId`. Add the "Steam" `vguiPopupWindow`, with `GetDesktopWindow()` called before the `SetEnvironmentVariableW` calls, the `GamingRepair` mark and `PROTON_WAIT_ATTACH`.
  5. Later, as L work after WoW64: build steam.c as a Wine builtin (PG Makefile.in:462-470 pattern), add `steam.exe=b` with the Locoland exception, and put SteamService.exe at `Steam\steam.exe`.
- **Test, no Steam:** new `bridge/check.sh` expectations:
  - the launcher runs the `system32` path;
  - a `.bat` target writes its marker through ShellExecute;
  - `steam.exe steam://store/480` reaches the stub `open(1)` (check.sh:97-99 already has the stub);
  - `steam.exe -- steam://rungameid/$SteamGameId` sent while the helper runs restarts it (the helper counts starts);
  - without `SteamGameId`, no registry values are written;
  - `FindWindowW(L"vguiPopupWindow", L"Steam")` succeeds from the game.

  Swift: the launcher command, the copies in both places, and the `removeSteamBridge` list.
- **Maintainer:** Palworld and SMITE 2 launch, and an in-game store link opens Mac Steam.
- **Cost:** M (L for step 5).
- **Dependencies:** P0-3 (same code). Step 5 needs WoW64.

**P1-2 Locale from Steam: Proton's `LC_ALL` rule plus `steamclient_init_registry`**
- **Proton:**
  - `init_wine` sets `LC_ALL` to `HOST_LC_ALL` if that is non-empty, and otherwise removes it (proton.py:1569-1575).
  - steam.exe calls lsteamclient's `steamclient_init_registry` (steam.c:206-221, called at 960). It writes the `language` and `Apps` keys and `setenv`s `LC_CTYPE` and `LC_MESSAGES` from the game's Steam language, with per-app hacks (identical in LSC/unixlib.cpp:490-602). The game inherits the Unix environment.
- **Ours:**
  - Mac Steam's `LC_ALL=en_US.UTF-8` (game-log header) passes through, so Wine reports en-US for both locales (W/dlls/ntdll/unix/env.c:735-737) on every Mac and for every game language.
  - `bridge/steam.c:86-97` copies only the registry writes. Its comment calls the Steam connection a reason, which isn't a macOS one.
- **Change:**
  1. `LaunchEnvironment.build`: `LC_ALL = HOST_LC_ALL` when non-empty, otherwise remove it. With no `LC_*` or `LANG`, Wine falls back to CoreFoundation (env.c:739-793). That is the macOS form of Proton's "host locale".
  2. `bridge/steam.c`: add Proton's `LoadLibraryW(L"lsteamclient")` / `GetProcAddress("steamclient_init_registry")` call **before `CreateProcessW`**, because the child inherits the Unix environment when it is created. Our current order starts the child suspended first (bridge/steam.c:240-245). Keep today's registry writes as the fallback when the call is missing or fails.
  3. `steamclient_init_registry` gets the UI language over IPC (`GetSteamUILanguage`, LSC/unixlib.cpp:506). The launcher's read of `registry.vdf` (`steam.uiLanguage()`, Launcher.swift:191) then becomes fallback-only. Drop it if the fallback never fires in practice: it reads a file that also holds account data.
  4. Four of Proton's locale names don't exist in macOS's `/usr/share/locale`: `ar_001`, `es_419`, `th_TH` and `vi_VN`. For those, `setlocale` fails and Wine falls back to the Mac's locale. Accept that and document it; add no table until a player reports it.
- **Test, no Steam:**
  - Swift: `LC_ALL` is removed, `HOST_LC_ALL` is copied, and a user's own `LC_ALL` in launch options is dropped as in Proton.
  - `bridge/check.sh`: with no Steam running, the init fails cleanly, the fallback keys are written (the existing expectation at check.sh:93), the helper still starts, and its environment has no `LC_ALL`.
- **Maintainer:**
  - In one game's Steam properties, set the language to Japanese on this fr-CA Mac. Confirm the game's system locale and code page. A CJK title, or a helper printing `GetSystemDefaultLocaleName`/`GetACP` run through the launcher, does this.
  - Confirm Palworld's language choice is unchanged.
- **Cost:** S + S.
- **Risk:** `steamclient.dylib` is loaded in the steam.exe process, as `steamclient.so` is under Proton.

**P1-3 Steam overlay** (spike first)
- **Proton:**
  - Steam's `LD_PRELOAD` passes through. Wine keeps it as `WINE_LD_PRELOAD` and `dlopen`s `gameoverlayrenderer.so` when the display driver loads, but never in `explorer /desktop` (VW dlls/ntdll/unix/virtual.c:965-985, called at :1006 and :1040; VW dlls/ntdll/unix/loader.c:598-640; VW programs/explorer/desktop.c:1287-1296).
  - `GameOverlayRenderer64.dll` is copied (proton.py:1118). Its exports are trampolined to lsteamclient's stubs, its DllMain is skipped (loader.c:2377-2380, 2416-2419), and its tier0/vstdlib imports are redirected (1203-1205).
  - Input blocking: lsteamclient's `__wine_steamclient_GameOverlayActivated` event, read by xinput1_3, dinput and hidclass.sys.
- **Ours:**
  - Mac Steam sends `STEAM_DYLD_INSERT_LIBRARIES=<MacOS>/steamloader.dylib:<MacOS>/gameoverlayrenderer.dylib` (game-log header), and we ignore it. `DYLD_INSERT_LIBRARIES` can't work anyway: wine.app is hardened without `allow-dyld-environment-variables` (wine-arm64/wine.entitlements; bundle.sh:196). It does have `disable-library-validation`, so `dlopen` of Valve's signed dylib is allowed.
  - LSC/lsteamclient.spec:62-76 already has the overlay stubs.
  - Nothing in our Wine tree reads the `GameOverlayActivated` event that lsteamclient creates.
  - Patch 0036 matches only `steamclient64.dll` (patch:151).
- **Change:**
  1. Spike, S-M: a Wine patch that, when `winemac.so` loads (Proton uses winex11's load), `dlopen(RTLD_NOW|RTLD_GLOBAL)`s the `gameoverlayrenderer.dylib` entry of `STEAM_DYLD_INSERT_LIBRARIES`. Skip explorer, as Proton does, and skip `steamloader.dylib`, whose role is unknown. Check Shift+Tab, F12 and toasts on a DXMT game, and the interaction with the MetalFX presenter's `nextDrawable` swizzle.
  2. If the spike works:
     - make it a proper patch;
     - widen 0036's name match and import redirect to `gameoverlayrenderer64` and write the `genuine-gameoverlayrenderer` marker (part 3 A3);
     - deploy Valve's `GameOverlayRenderer64.dll`;
     - port the `GameOverlayActivated` consumers in xinput1_3, dinput and hidclass.sys.

  `LauncherTests.swift:369` (`DYLD_INSERT_LIBRARIES` unset) stays true, because the route is `dlopen`.
- **Test, no Steam:**
  - `wine-arm64/check.sh steamclient` extended: a genuine `GameOverlayRenderer64.dll` from the cache loads with its exports redirected and its DllMain skipped.
  - A tiny test dylib named in `STEAM_DYLD_INSERT_LIBRARIES` is `dlopen`ed once in a game process, and not in explorer. It writes a marker file.
- **Maintainer:**
  - Overlay, F12 and toasts in Palworld and SMITE 2.
  - Also check a Mac-native game under `macneutron-native`: if its overlay is gone in Linux mode, `passthrough` should set `DYLD_INSERT_LIBRARIES` from `STEAM_DYLD_INSERT_LIBRARIES`. Native games are not our hardened wine.app; this is a macOS-only item.
- **Cost:** L, including the S-M spike.
- **Dependencies:** P0-1, for the DLL.

**P1-4 Install scripts and redistributables: find out first, then the cheap parts**
- **Proton:** proton.py has no install-script code. Linux Steam runs `<tool> run <client>/legacycompat/iscriptevaluator.exe legacycompat\evaluatorscript_<appid>.vdf`, and Proton's `run` is generic (proton.py:2160-2164). Mac `steamclient.dylib` has the same job (`CRunWindowsCompatInstallScriptJob` and the format strings). Valve's wine.inf marks .NET 3.5 to 4.8 and XNA 4.0 as installed, so their installers are skipped (VW wine.inf.in:1497-1505).
- **Ours:**
  - Mac Steam has no `legacycompat`. `RunningInstallScript` ends within a second with no process (console_log.txt), and launcher.log has 0 `run` verbs.
  - `_CommonRedist` (VC 2012/2022, DirectX June 2010, .NET 4.5.2) is downloaded and never run. All its installers are i386.
  - Launcher.swift:61-65 exits 0 for an i386 `run` target. Exit 0 is correct (a failed script would block the game), but players never hear about it.
  - Valve's `bins_win64` has no `iscriptevaluator.exe`.
- **Change:**
  1. **Probe, maintainer, S:**
     - The `%s` has two plausible values: the bundle's MacOS folder (`STEAM_COMPAT_CLIENT_INSTALL_PATH`) or `~/Library/Application Support/Steam`. Place a 64-bit stub (the bridge helper, renamed) at `legacycompat/iscriptevaluator.exe` under both, in one probe.
     - Launch a throwaway app with an install script (Spacewar 480 if it has one, or 3DMark Demo 231350) and look for `verb=run` in launcher.log. That is all the probe shows. Steam reads progress from `standalone_installscript_progress_<appid>.vdf`, so a stub that exits at once may leave Steam waiting.
     - Remove both stubs afterwards.
  2. Add the CommonRedist keys at prefix creation: `reg add` next to MNC/PrefixManager.swift:113-124. S.
  3. If the probe shows Steam calls `run`: find Valve's package for the evaluator. The Linux client packages are the candidates, and it is probably i386, so it needs WoW64. Add it to the Valve spec (part 3 A6). If Steam doesn't call `run`: the documented deviation stands. Our own interpreter for the `installscript.vdf` `Registry` section waits for the first game that fails without it (P2).
  4. When `run` starts arriving, make the i386 skip visible: notify once per app and keep exit 0.
- **Test, no Steam:** Swift: `prepareNew` issues the CommonRedist `reg add`s.
- **Maintainer:** the probe.
- **Cost:** S (steps 1, 2 and 4). M for an interpreter, if one is ever needed.
- **Dependencies:** WoW64, for any redistributable and probably the evaluator.

**P1-5 Pass `STEAM_COMPAT_PROTON` through** (verbs-env and tool-registration rows)
- **Proton:** Steam sets it for "proton" tools. proton.py leaves it in the environment, so steamclient inside the game sees it (Mac dylib: `STEAM_COMPAT_PROTON`, `STEAM_COMPAT_PROTON_SUPPRESS`).
- **Ours:** removed (MNC/LaunchEnvironment.swift:10-12; cloud spec D5: "what the predicate gates is unknown").
- **Change:** delete those three lines and their test, and amend cloud D5. A player who needs it gone can use `/usr/bin/env -u STEAM_COMPAT_PROTON %command%`; no new switch is needed.
- **Test:** Swift: it passes through.
- **Maintainer:** A/B launches of Palworld, with and without, comparing cloud sync, achievements, overlay and controller. Only meaningful after §0, because Steam sets it only for the proton-named tool.
- **Cost:** S.

**P1-6 Controllers through Steam Input: probe, then port or document**
- **Proton:** SDL 2.30's Steam virtual-gamepad integration (CW #23185) plus `SDL_GAMECONTROLLER_IGNORE_DEVICES` handling: VW dlls/ntdll/unix/file.c:3157-3215, VW dlls/xinput1_3/main.c:488 and :1180, VW dlls/winebus.sys/bus_udev.c:1556 and :1743, VW dlls/winebus.sys/bus_sdl.c:932-939.
- **Ours:** none of it. winebus builds only the iohid backend, and `is_xbox_gamepad` knows only VID 0x045e (W/dlls/winebus.sys/unixlib.h:168).
- **Change:**
  1. Maintainer probe: with a controller connected, Steam Input on for a Windows game and the game running, run `ioreg -r -c IOHIDDevice -l | /usr/bin/grep -B2 -A6 '"VendorID" = 10462'`, and check whether the game sees a pad.
  2. If a 0x28DE/0x11FF virtual pad appears: port the iohid gamepad flag, `is_sdl_ignored_device` in `bus_iohid`, and the xinput and hidclass hunks (M-L).
  3. If not: N/A, documented. Mac Steam's virtual pads don't reach HID, and kernel extensions are ruled out.
- **Cost:** S for the probe.

### P2: polish and parity

| # | Proton | Ours | Change | Test | Cost |
|---|---|---|---|---|---|
| P2-1 | Tools live in `<Steam root>/compatibilitytools.d` (PG Makefile.in:1523-1525; template :7-14) | Linked into the Steam bundle (SteamLocation.swift:24; SteamPlayMode.swift:42, 48-52, 246-254) | Point `link(_:)` at `steam.root/compatibilitytools.d`, remove the bundle links in the migration, and re-run app-design §10 task 1's registration probe. `steam_dev.cfg` must stay in the bundle, so a client update still ends Steam Play mode: the gain is Proton's location and one wipe-able item instead of three | Swift `SteamPlayMode` with temp roots. Maintainer: `Registering tool macneutron-proton` from the new path | S |
| P2-2 | `STEAM_COMPAT_CONFIG`: `cmdlineappend:` with `\,` escapes appended to the game (proton.py:1533-1545, 2124) | Ignored; Mac Steam hasn't sent it yet (literal in the dylib) | Parse `cmdlineappend:` and log other tokens | Swift `Launcher` | S |
| P2-3 | `STEAM_COMPAT_APP_ID` where `SteamAppId` is absent (proton.py:1459; steam-runtime-tools `steam-compat-tool-interface.md`: `SteamAppId` is not set for install-script runs) | `SteamAppId` or "0" (CompatContext.swift:23) | `SteamAppId` (non-empty, not "0"), then `STEAM_COMPAT_APP_ID`, then "0" | Swift `CompatContext` | S |
| P2-4 | `compatmanager_layer_name "proton"` (toolmanifest_x86_64.vdf:7) | Missing (RuntimeInstaller.swift:68-69) | Add the key; the Mac dylib knows it | Swift `RuntimeInstaller` | S |
| P2-5 | `[SteamClient64]` seeded at prefix creation: `ActiveProcess\SteamPath`, `SteamClientDll` always, `PID 0xfffe`, plus `OpenProcess(0xfffe)` = self (VW wine.inf.in:1401-1408; VW dlls/kernelbase/process.c:1372-1384). Steam folder in the DLL search path (loader.c:89) | steam.exe writes most keys at launch, but no `ActiveProcess\SteamPath`, and `SteamClientDll` only if the file exists (bridge/steam.c:98-107). Nothing for `runinprefix` or `MACNEUTRON_NO_STEAM_BRIDGE`. W/dlls/ntdll/loader.c:93 has no Steam folder | Add `ActiveProcess\SteamPath` and always write `SteamClientDll` (steam.c). Later: an inf `AddReg` or `prepareNew` seed, the `0xfffe` kernelbase hunk and the `system_path` entry (Wine patch) | `bridge/check.sh` registry expectation; Wine: a helper `LoadLibrary("Steam.dll")` by bare name | S each |
| P2-6 | `sync_appcache`: copies `appcache/librarycache/<appid>` into the prefix's Steam folder at every launch (proton.py:1266-1277, 2030; PG bc896217 copies instead of linking) | Missing | Copy from `SteamLocation.root/appcache/librarycache/<appid>` (on Mac, `STEAM_COMPAT_CLIENT_INSTALL_PATH` is the bundle) | Swift `PrefixManager` | S |
| P2-7 | `PROTON_CRASH_REPORT_DIR` → `WINE_CRASH_REPORT_DIR`; winedbg writes `<time>_appid-<id>_crash.log` (proton.py:1945-1946; VW programs/winedbg/tgt_active.c:832-872) | Mac Steam sets `/tmp/proton_crashreports` and we ignore it | Wine patch plus a one-line env map | Wine: a crashing helper writes the file | M |
| P2-8 | `STEAM_COMPAT_SHADER_PATH`: Steam owns that cache, sizes it and clears it | DXMT pipelines go to `compatdata/<id>/dxmt-pipelines` (LaunchEnvironment.swift:31-33, ShaderPrecache.swift:17) | Default `DXMT_PIPELINE_RECORD` to `STEAM_COMPAT_SHADER_PATH`, so Steam's size display and "delete shader cache" work. Maintainer choice: a clear forces a re-record | Swift `LaunchEnvironment` | S |
| P2-9 | Profile `steamuser` (VW dlls/advapi32/advapi.c:45-72); no links to the Linux home folders (Valve d6c34d05d) | `users/$USER` plus the `steamuser` link (ProfileLinks.swift:18-35); Wine links `Documents` and the other folders to `~/` (W/dlls/shell32/shellpath.c:2814-2849) | Maintainer decision. For **new** prefixes only, recorded at creation: `USER=steamuser` in Wine's environment (no Wine patch; also needed in `terminate()` and getcompatpath), and a shell32 patch that skips home links. Existing prefixes keep their D3 links | Swift (`PrefixManager` and `ProfileLinks` with a fresh prefix); `bridge/check.sh` `%USERPROFILE%` | M |
| P2-10 | `destroyprefix` (proton.py:2172-2173) | Not a verb (Verb.swift:2-8) | **None.** The Mac `steamclient.dylib` has no `destroyprefix` literal, and our prefix holds the player's data. Document it | — | — |

**Outside this plan** (Proton runtime parity, not the Steam connection; a separate runtime-parity plan):
- FEX defaults (X87ReducedPrecision, MaxInst 500, SilentLog with logging).
- Symlinked builtin DLLs with a default prefix (about 1.2 GB per prefix today).
- Fonts.
- wine-mono.
- Proton's `n,b` defaults for MSVC/UCRT.
- The `SteamGameId`-keyed per-app Wine hacks.
- `PROTON_LIMIT_RESOLUTIONS`, heap, CPU topology and address-space hacks.
- `PROTON_*` launch-option aliases.
- `WINEARCH` removal.
- `winebth.sys=d`, `opencl=n,d`.
- `winemenubuilder`.
- ICU 68.
- `MIMALLOC_DISABLE_REDIRECT`.

---

## 3. Amendments to the approved Valve steamclient spec

`docs/superpowers/specs/2026-10-12-macneutron-valve-steamclient-design.md`

- **A1, §2 facts.** Add: "`bins_win64` (manifest version 1788652215, `sha2 93f5b6…`, 51 files) holds every file Proton copies from Linux Steam's `legacycompat` (proton.py:1116-1124):

  | File | Machine |
  |---|---|
  | `steamclient64.dll` | AMD64 |
  | `GameOverlayRenderer64.dll` | AMD64 |
  | `steamclient.dll` | i386 |
  | `Steam.dll` | i386 |
  | `GameOverlayRenderer.dll` | i386 |
  | `bin/SteamService.exe` | i386, console |

  It has no `iscriptevaluator.exe`." The current §2 line 25 says "tier0_s64.dll, vstdlib_s64.dll and more". These are not deployed: Proton redirects those imports to ntdll stubs.
- **A2, D3 (what to keep).** Change it to: extract all six into `Valve/<version>/` now. Flatten `bin/` and keep Valve's names. Check the PE machine of each against the A1 table. The cost is about 28 MB on top of the two files already planned, and WoW64 then never needs a second download. D4 (refresh only when `sha2` changes) is unchanged.
- **A3, §4.2 capability.** Keep `genuine-steamclient` for `steamclient64.dll`. Add a second marker, `genuine-gameoverlayrenderer`, which `wine-arm64/build.sh` writes once 0036 also matches `gameoverlayrenderer64`, both the trampolines and the tier0/vstdlib import redirect (loader.c:2378-2380, 1203-1205). `ToolLayout.supportsGenuineOverlayRenderer` reads it. State it plainly: **never deploy `GameOverlayRenderer64.dll` without that marker.** Its real DllMain would hook the game's rendering.
- **A4, §4.3 deployment becomes the Proton prefix layout:**

  | Prefix path | Source | Condition |
  |---|---|---|
  | `Steam\steamclient64.dll` | Override, then cache, then lsteamclient | Unchanged |
  | `Steam\GameOverlayRenderer64.dll` | Cache | `genuine-gameoverlayrenderer`; otherwise absent, as Proton does when the file is missing (proton.py:1123) |
  | `Steam\steamclient.dll`, `Steam\Steam.dll`, `Steam\GameOverlayRenderer.dll` | Cache | A WoW64 runtime (separate spec) |
  | `Steam\steam.exe` | Our helper (tool `bin/steam.exe`) | Until steam.exe is a Wine builtin with `steam.exe=b` **and** WoW64 exists, then Valve's `SteamService.exe` (proton.py:1119) |
  | `windows\system32\steam.exe` | Our helper | Always (P1-1); the launcher runs this one |

  Why `Steam\steam.exe` waits: SteamService.exe is i386, and Wine takes the process machine from the image (W/dlls/ntdll/unix/process.c:262-292). Under Proton nothing ever executes that file, because `steam.exe=b` hands every run of it to an i386 builtin helper. On our runtime it can't start. Today the launcher itself runs that path (SteamBridge.swift:7, Launcher.swift:155): placing it before P1-1 breaks every launch, and placing it before the builtin breaks the command handler.

  Implementation notes:
  - `SteamBridge.prefixFiles` must stop mapping `steamHelper` to `Steam/steam.exe` when that switch happens. `install()` compares size and modification time with the mapped source, so the old mapping would overwrite Valve's file at every launch.
  - "Absent" means *deleted* at deployment when its condition fails. A prefix that got `GameOverlayRenderer64.dll` from a newer runtime keeps it after a downgrade, because nothing overwrites it, unlike `steamclient64.dll`. This is Proton's tracked-files removal on downgrade (proton.py:648-670) applied per file. The i386 files follow the same rule once WoW64 exists.
  - `removeSteamBridge` (PrefixManager.swift:237-243) must remove every path in this table.
- **A5, §6 out of scope.** Replace "GameOverlayRenderer64.dll, Steam.dll and SteamService.exe, until a game needs them" with the A4 conditions. Proton deploys these files unconditionally; our conditions are the runtime reasons: the 0036 marker, WoW64 and a builtin helper.
- **A6, new section: iscriptevaluator.exe.** Not covered.
  - Mac Steam looks for `%s/legacycompat/iscriptevaluator.exe` (`steamclient.dylib` strings).
  - It is not in `bins_win64`, and its Valve package is unknown (the Linux client packages are the candidates).
  - Decide after the P1-4 probe. If adopted, it comes from Valve's CDN through `ValveClient`, the same way.
  - Don't place Valve files in the Steam bundle's `legacycompat`: client updates wipe the bundle.
- **A7, §5 tests.**
  - The fake zip holds all six names, with right and wrong machines.
  - One row per A4 line, including marker gating.
  - `removeSteamBridge` removes every name.
  - The `release/release.sh` guard covers all Valve names (`steamclient*`, `GameOverlayRenderer*`, `Steam.dll`, `SteamService*`, `tier0_s*`, `vstdlib_s*`).

---

## 4. Deviations to document in the Steam bridge spec

`docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md`

First, replace its stale facts: §2.8 and the §3 diagram still describe the Rosetta x86_64 runtime, winecx `lsteamclient` and x86_64 mingw. Today lsteamclient is built in-tree from `proton_11.0` (`wine-arm64/deps.pins:23`), and steam.exe is arm64 mingw (Makefile:30).

**Launching the tool**
- **D-1:** Steam runs `/bin/macneutron launch %verb%`, an arm64 Mach-O, not `/proton`: a clean Mac has no Python (RuntimeInstaller.swift:64-69; arm64-release spec R0b).
- **D-2:** No `require_tool_appid` and no `use_sessions`: macOS has no Steam Linux Runtime or pressure-vessel. `STEAM_COMPAT_MOUNTS` and the runtime adverb have no consumer.
- **D-3:** Compat tools apply only with `@sSteamCmdForcePlatformType linux` in the bundle's `steam_dev.cfg`. A second tool, `macneutron-native`, keeps Mac builds mounted (SteamPlayMode.swift:19, 222-244).
- **D-4:** A local tool (AppID 0) installed and updated by MacNeutron.app: only Valve can publish a tool app.
- **D-5:** After the game, a second `wineserver -w` keeps Steam's "running" state until the prefix is empty. Mac Steam has no subprocess reaper (Launcher.swift:115-117).
- **D-6:** SIGTERM and SIGINT run `wineserver -k` for Steam's Stop button, for the same reason (Launcher.swift:140-150).
- **D-7:** `STEAM_COMPAT_CLIENT_INSTALL_PATH` is the Steam bundle's MacOS folder, which holds `steamclient.dylib`, not the Steam root. Library and appcache paths come from `SteamLocation` instead (SteamBridge.swift:45-50).
- **D-8:** `libraryfolders.vdf` leaves out Proton's entry 0, the client folder: on macOS it is not a library.
- **D-9:** Launch options run without a shell, so `VAR=x %command%` must be `/usr/bin/env VAR=x %command%`. Otherwise Steam fails with OS Error 260.

**Steam client bridge**
- **D-10:** `steamclient64.dll` falls back to lsteamclient when no Valve DLL is cached (first launch offline, or after Remove): Mac Steam ships no `legacycompat`.
- **D-11:** Patch 0036 writes its trampolines PE-side (RW, then restore), not through Unix `mprotect(RWX)`: Apple Silicon refuses W^X violations. lsteamclient loads from `system32` only, and the jump into the genuine DLL's entry point is replaced by `LDR_DONT_CALL_DLLMAIN` alone.
- **D-12:** `steam://` URLs and forwarded steam.exe commands go to Mac Steam through `open(1)`, not the `steam` or `steam-runtime-steam-remote` binaries, which macOS doesn't have. Non-URL `-` options other than `-applaunch` are logged and dropped.
- **D-13:** lsteamclient patch 0003: `Steam_IsKnownInterface` and `Steam_NotifyMissingInterface` are optional, because Mac `steamclient.dylib` doesn't export them. Patch 0001: native key codes are 0, because macOS has no X11 keysyms. Patches 0001 and 0002: `-DNOMINMAX` and `-lc++` for the build.
- **D-13a:** `ValvePlatformMutex` is the constant `C:/Program Files (x86)/Steam/steam.exe` (bridge/steam.c:230), which is what Windows Steam sets. Proton uses the helper's own path from `GetModuleFileNameW` (steam.c:950-958), which would be `C:/windows/system32/steam.exe` after P1-1.
- **D-14:** No i386 Valve files, no 32-bit lsteamclient, and SteamService.exe isn't at `Steam\steam.exe` until WoW64 exists and the helper is a builtin (part 3 A4). Until then that path holds a copy of our helper.

**Platform**
- **D-15:** msync instead of fsync and ntsync (no futex or ntsync on macOS), and DXMT on Metal instead of DXVK and vkd3d (no Vulkan). The Proton graphics and sync variables don't apply.
- **D-16:** Install scripts don't run: Mac Steam has no `legacycompat/iscriptevaluator.exe`, and our runtime can't run the i386 redistributable installers. Revise after the P1-4 probe.
- **D-17:** The locale comes from CoreFoundation (the Mac's settings) once `LC_ALL` is dropped, not from `HOST_LC_ALL`, which Mac Steam doesn't send. Four Steam languages (Arabic, Latin American Spanish, Thai, Vietnamese) fall back to the Mac's locale, because macOS lacks those locale names.
- **D-18:** Overlay, after the spike: `gameoverlayrenderer.dylib` from `STEAM_DYLD_INSERT_LIBRARIES` is `dlopen`ed when winemac loads, because the hardened wine.app ignores `DYLD_INSERT_LIBRARIES`.

**Logs and settings**
- **D-19:** Logs are `~/Library/Logs/MacNeutron/steam-<appid>.log`, appended and rotated past 50 MB, with account values and secrets redacted (LauncherLog.swift:18-34; Launcher.swift:200-219). Per-game settings come from the app, not `user_settings.py`.
- **D-20:** `PROTON_CRASH_REPORT_DIR` is ignored until P2-7, because Mac has no Valve crash upload. `destroyprefix` isn't implemented, because the Mac client never sends it (P2-10).
- **D-21:** `HKCU\Software\Wine\X11 Driver\EmulateModeset=Y`: fullscreen mode changes stay virtual, so the Mac display is never switched (PrefixManager.swift:123-124).

Any revisit item in part 1 that the maintainer decides to keep becomes a D-line with its stated reason.

## Part C. The review (for reference)

# Adversarial review: the Proton 11 parity plan for MacNeutron's Steam connection

Read-only throughout. I launched nothing and changed nothing. Home is written as `~` and no account values appear. Abbreviations:
- `P` = the proton-steam scratchpad (proton.py, steam.c, loader.c, unix-loader.c).
- `PG` = proton-git `origin/proton_11.0`.
- `VW` = ValveSoftware/wine proton_11.0 (fetched copies under `/private/tmp/claude-501/proton-parity/wine-steam/pw`).
- `W` = `build/wine-arm64-src/wine`.
- `LSC` = `build/wine-arm64-src/lsteamclient/lsteamclient`.

## Claims I checked that hold

These need no rework:
- **Proton's prefix code:**
  - drive code: proton.py:299-352, 1457, 1259, 2160-2164
  - Steam-file copy: 1113-1124
  - overrides: 1515-1528
  - `LC_ALL` rule: 1569-1575
  - verbs: 2150-2184
- **steam.c structure:** main 916-1063, command handler 652-748, `setup_steam_files` 750-816, `setup_steam_registry` 206-221. ValvePlatformMutex is an environment variable, not a registry value (steam.c:949-958).
- **Proton's loader:** the trampolines and DllMain skip (loader.c:2377-2420), the tier0/vstdlib redirect (1201-1210), and the Steam folder in `system_path` (loader.c:89; ours W/dlls/ntdll/loader.c:93 lacks it).
- **Valve's wine.inf:** `[SteamClient64]` is at VW wine.inf.in:1401-1408 and is used by the ntamd64/ntarm64 installs. The `HKCR\steam` command is at :316.
- **Drive lookup:** `find_drive_nt_root` takes the deepest drive (W/dlls/ntdll/unix/file.c:4229-4268). So `s:` gives `S:\steamapps\common\…`, and drive_c paths still resolve to C:.
- **steam.exe=b would break launches today:** Wine ignores non-builtins (W/dlls/ntdll/unix/virtual.c:3898-3904), and an absent system-dir builtin loads only during prefix bootstrap (W/dlls/ntdll/unix/loader.c:1529).
- **lsteamclient bug:** `buffer` is freed uninitialised (LSC/unixlib.cpp:949-959; W/dlls/ntdll/unix/file.c:4530-4534). Both fixes, `f46fd902` and `b94341c4`, are on `origin/bleeding-edge`.
- **Locales:** macOS lacks exactly `ar_001`, `es_419`, `th_TH` and `vi_VN` in `/usr/share/locale`.
- **Valve's zip:** `bins_win64.zip` holds 51 files and no `iscriptevaluator.exe`. `SteamService.exe` is PE32 i386 console; `GameOverlayRenderer64.dll` is x86-64.
- **Entitlements:** `wine-arm64/wine.entitlements` has `disable-library-validation` and lacks `allow-dyld-environment-variables`.
- **§0 rename regression:**
  - compat_log.txt:13441 (21:15) registered `macneutron-proton`; compat_log.txt:13810 (21:44) registered `macneutron`.
  - The rename commit `8dd6086` is from 20:27 local time.
  - Also, the 21:15 session registered **only** `macneutron-proton`, never the two-entry migration vdf (RuntimeInstaller.swift:34-50). That supports the "an older or different app build rewrote things" theory.
- **HKCR and A4 are consistent:**
  - P1-1 step 3 points `HKCR\steam` at `Steam\steam.exe -- "%1"`.
  - A4 swaps Valve's i386 SteamService.exe into that path only after the helper is a builtin and `steam.exe=b` exists. So the URL handler always reaches a helper.

## Problems

### H1 (High): P1-3's overlay design will most likely give a partial overlay. It also misdescribes Proton.

**Evidence: the Mac overlay dylib.**
- `dyld_info -arch arm64 -fixups ~/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS/gameoverlayrenderer.dylib` shows a `__DATA,__interpose` section binding 15 functions:
  - `CGLChoosePixelFormat`, `CGLFlushDrawable`, `glSwapAPPLE`
  - `HideCursor`, `InitCursor`, `ShowCursor`
  - `CFRunLoopRun`, `CFRunLoopRunInMode`
  - `CGAssociateMouseAndMouseCursorPosition`, `CGCursorIsVisible`, `CGDisplayHideCursor`, `CGDisplayShowCursor`, `CGDisplayMoveCursorToPoint`, `CGGetLastMouseDelta`, `CGWarpMouseCursorPosition`
- It also has `__mod_init_func`, `__objc_classlist` and `__objc_catlist` sections.
- dyld applies `__interpose` tuples only for images present at launch (`DYLD_INSERT_LIBRARIES`). A `dlopen` runs the constructors and ObjC categories, so the Metal swizzles may work, but the interposes do not. Please confirm this in the spike.
- What a dlopen'd overlay would lose: cursor release and mouse capture while the overlay is open, its run-loop event pump, and the GL hooks. winemac makes exactly these calls.

**Evidence: what Proton actually does.**
- Proton's main path is `LD_PRELOAD` processed by the dynamic linker at exec, in every Wine process.
- The `winex11.so` dlopen is only a fallback: `if (getenv("LD_PRELOAD") || !(preload = getenv("WINE_LD_PRELOAD"))) return;` (VW dlls/ntdll/unix/virtual.c:973).
- Explorer `/desktop` is excluded by stripping `LD_PRELOAD` at exec (VW dlls/ntdll/unix/loader.c:606-636). Explorer restores it for its children (VW programs/explorer/desktop.c:1287-1296).

**Correction.**
1. The spike's test dylib must carry an `__interpose` tuple, for example `CGWarpMouseCursorPosition`, and check whether it fires after `dlopen`. This is testable without Steam.
2. Name the second option in the plan: add `com.apple.security.cs.allow-dyld-environment-variables` to wine.app. That is a signing decision on our own app, not Reduced Security. The launcher would then set `DYLD_INSERT_LIBRARIES` to the gameoverlayrenderer entry only, never `steamloader.dylib`, whose only dependency is `drmstub.arm64`. Port Proton's explorer strip with it.
3. Rewrite D-18 to match.
4. Decouple `GameOverlayRenderer64.dll` from this spike. The 2026-09 DLL imports only ADVAPI32, CFGMGR32, GDI32, IMM32, KERNEL32, OLEAUT32, PSAPI, USER32, WINMM and ole32 (`llvm-objdump -p` of the extracted copy). It has no tier0/vstdlib imports, so A3's import-redirect condition is moot. The deployment marker needs only 0036's name match (P/loader.c:2379-2380, DllMain skip 2416-2419).

### M1 (Medium): P0-3 misstates Proton's validation fallback, and omits Proton's opt-out.

**Evidence.**
- When validation fails, `get_validated_steamapps_parent` returns `orig_path`, the `steamapps` folder itself (proton.py:299-311). The game then sees `S:\common\Game`. That is the layout that broke Natural Selection 2 and The Talos Principle (PG `45dc0d75`, `501f7cc2`, `7714f42e`; CW #27297, #27377) until the parent-folder fix `d93f8a66`. The plan's test instead says the link is "not made" in those cases.
- Proton's player-facing switch is `PROTON_SET_GAME_DRIVE=0` (proton.py:1772, `check_environment` 1629-1636). It is missing from P0-3.
- History correction: S: became the default in `7b70313e` (2026-01-20). `d93f8a66` and `fa493fa6` (2026-08-04) are the parent-folder fix and the removal of the per-game disables.

**Correction.**
- Follow Proton's fallback, or record "no S: when validation fails" as a D-line. It has no macOS reason, so it is the maintainer's call. On Mac Steam the branch is nearly unreachable, because `STEAM_COMPAT_LIBRARY_PATHS` is `~/Library/Application Support/Steam/steamapps` (Palworld game-log header).
- Honour `PROTON_SET_GAME_DRIVE=0` (as `/usr/bin/env PROTON_SET_GAME_DRIVE=0 %command%`) as the per-game way out of a regression.

### M2 (Medium): P0-3's regression risk for working games is understated.

**Evidence.**
- The path change moves every game's visible exe path from `Z:\Users\…\Library\Application Support\Steam\…` to `S:\steamapps\…`. That affects:
  - launchers and mod managers that store install paths;
  - anything keyed on the exe path (DXMT's internal pipeline DB keying is unverified);
  - Proton's own temporary disables after the default.
- No Mac failure caused by Z: has been observed in the Palworld, SMITE 2 or 3DMark logs.

**Correction.**
- Keep it P0 only with a gate: M1's fixes, plus a maintainer smoke on Palworld, SMITE 2 and one launcher-based game.
- Timberborn runs through passthrough and is unaffected, but check it anyway.
- Otherwise call it P1.

### M3 (Medium): P1-1 drops Proton's per-app helper path.

**Evidence:** proton.py:2109-2115 runs `c:\Program Files (x86)\Steam\steam.exe` instead of system32 for 311210 (Call of Duty: Black Ops III), 1985810 (Black Ops Cold War) and 1549250 (Undecember).

**Correction:** keep that three-appid list in the launcher once the default moves to `C:\windows\system32\steam.exe`. Our helper copy stays at the Steam path (A4).

### M4 (Medium): P1-2's "fallback when the init fails" can't be built as written, and its steps are not independent.

**Evidence.**
- `steamclient_init_registry` returns void (LSC/steamclient_main.c:630-634).
- The unix side returns 0 even after "Failed to connect to Steam" (LSC/unixlib.cpp:706-726). The PE side can never see a failure.
- `setenv("LC_CTYPE"/"LC_MESSAGES", locale, FALSE)` (LSC/unixlib.cpp:598-602) does nothing while `LC_ALL` is set, because `LC_ALL` overrides both in `setlocale(LC_ALL, "")` (W/dlls/ntdll/unix/env.c:735-737).

**Correction.**
- Write today's keys first, then call init, which overwrites them on success.
- Treat steps 1 and 2 as one change (S-M), not "S + S".

### M5 (Medium): P1-5 flips a default that two working games depend on, before any evidence.

**Evidence.**
- D5's reason is "keep the known-good pre-rename environment" (LaunchEnvironment.swift:10-12; cloud spec lines 19-21, 35).
- In Mac `steamclient.dylib` strings, `STEAM_COMPAT_PROTON_SUPPRESS` sits next to `SteamUtils011`, `SteamTenfoot` and `SteamDeck`. That suggests a game-visible ISteamUtils-level predicate, in a code path Valve never ships on macOS.

**Correction.** Make the Palworld and SMITE 2 A/B the gate, run only after §0 is fixed, and change the code only after it passes.

### M6 (Medium): P1-2 leaves the unix side in the C locale on macOS.

**Evidence.**
- Mac Steam sends no `LANG`. The Palworld header has only `LC_ALL=en_US.UTF-8` and `__CF_USER_TEXT_ENCODING`.
- Dropping `LC_ALL` therefore puts every unix library in the game process, `steamclient.dylib` included, in the "C" locale. On Linux, Proton's pop leaves `LANG` in place.
- Wine itself is unaffected, since its Apple branch forces UTF-8 (W/dlls/ntdll/unix/env.c:201-206).

**Correction.** Add a test with a non-ASCII home or library path (lsteamclient path conversion, Steam Cloud) before shipping.

### L1 (Low): P0-2 is a priority inflation.

**Evidence.**
- The bad free happens only when `get_nt_path` fails, which takes an empty or invalid path. No crash has been observed.
- The url_unescape bug affects only non-ASCII `file://` URLs.
- The plan's own test (`build.sh` plus `check.sh steamclient`) exercises neither fix.

**Correction.** Make it P1: a cheap upstream cherry-pick that rides along with the next lsteamclient rebuild.

### L2 (Low): `ActiveUser` is not an open question.

**Evidence:** neither steam.c, proton.py, lsteamclient nor VW wine.inf.in:1394-1408 writes it.

**Correction:** under the Proton rule, drop it now. Remove bridge/steam.c:104-105 and the `loginusers.vdf` read (Launcher.swift:193-198), which is also an account-data read.

### L3 (Low): `steamclient_init_registry` ignores a missing `steamclient.dylib`.

**Evidence.**
- It ignores `load_steamclient()`'s result (LSC/steamclient_main.c:632), then dereferences a NULL `p_CreateInterface` (LSC/unixlib.cpp:711, after the dlopen failure at 780-785).
- Wine hands the access violation back to the caller (W/dlls/ntdll/unix/signal_arm64.c:1083-1136). So "fails cleanly" holds only through fault recovery.
- The launcher carries on when the dylib is missing (Launcher.swift:186-188).

**Correction.** Add a `bridge/check.sh` case with a missing client folder. Optionally add a one-line lsteamclient guard.

### L4 (Low): the P1-4 probe stub would make Steam record a failed install script.

**Evidence:** the renamed helper would call `CreateProcessW` on `legacycompat\evaluatorscript_<id>.vdf` and exit 1 (bridge/steam.c:240-243).

**Correction:** use an exit-0 stub that writes a marker file.

### L5 (Low): the GamingRepair port does nothing on Mac.

**Evidence:** steam.c:75-90 only opens an existing `HKLM\SOFTWARE\Valve\Steam\Apps\<id>` key in the 32-bit view. Install scripts create that key, and they never run here.

**Correction:** drop it from P1-1 until P1-4 lands.

### L6 (Low): the P1-1 restart test would fail against a faithful port.

**Evidence:** Proton restarts only after the child has exited; otherwise it logs "Child is still running, not restarting" (steam.c:1013-1023).

**Correction:** the test must have the child exit around the signal.

### L7 (Low): D-13a's reason is unverified.

**Evidence:** "What Windows Steam sets" is not established. The code's own reason is letter case (bridge/steam.c:229). Proton uses `GetModuleFileNameW` (steam.c:950-958).

**Correction:** port Proton's value, or restate the reason.

### L8 (Low): D-20's reason is undercut by the evidence.

**Evidence:** Mac Steam sets `PROTON_CRASH_REPORT_DIR=/tmp/proton_crashreports` for our tool (Palworld header). "Mac has no Valve crash upload" is therefore unverified.

**Correction:** call it "not ported yet".

### L9 (Low): A5's wording is wrong.

**Evidence:** it says "Proton deploys these files unconditionally", but Proton copies only `if os.path.isfile` (proton.py:1123).

**Correction:** fix the wording.

### L10 (Low): P2-1 and the P1-3 passthrough idea touch Timberborn.

**Evidence and correction.**
- Moving the tool links must also rewrite the invariant in app-design spec line 179 ("steam_dev.cfg only if both tools resolve through B/compatibilitytools.d"). Otherwise Steam Play mode toggles off.
- Setting `DYLD_INSERT_LIBRARIES` in passthrough needs a Timberborn before-and-after test.

## Licence

**Verdict:** fine.
- `git ls-files` shows no Valve binaries, and `build/` is gitignored.
- `check.sh` uses `build/valve/…` or `VALVE_STEAMCLIENT64` (wine-arm64/check.sh:9, 805).
- `bundle.sh` touches no Valve DLL. It ships only lsteamclient's own licence (136-142).
- All extra files come from the same Valve CDN zip to the user's Mac. iscriptevaluator.exe, if it is ever adopted, follows the same model and must not go into the Steam bundle.

**To do:** the A7 `release/release.sh` guard does not exist yet; the only hits are lines 70 and 361. Add it before any deployment change. Extend its name list with zip members the plan doesn't name: `bin/SteamService.dll`, `gameoverlayui*` and `SteamOverlayVulkanLayer*`.

## Untestable without Steam (maintainer only)

- P0-1 Palworld
- P0-3 real game paths
- P1-2 init success and per-game language
- P1-3 beyond the interpose unit check
- the P1-4 probe
- the P1-5 A/B
- the P1-6 `ioreg` probe
- P2-1 registration
- P2-8 Steam's "delete shader cache"
- §0 (needs the installed vdf, which is off-limits to this research)

Testable now:
- the `bridge/check.sh` and Swift rows
- the interpose-after-`dlopen` check
- the missing-dylib init case

## Corrected P0 list

0. **§0: the tool-name regression.** Find the build that rewrote the vdf and the mappings at 21:44, then redo the migration. This blocks Cloud and P1-5.
1. **P0-1: Valve's `steamclient64.dll` by default,** per the approved spec, plus A2 extraction and the A7 guard. No overlay or SteamService deployment in P0.
2. **P0-3: the S: drive, gated.** It needs the Proton-faithful fallback or a documented D-line, the `PROTON_SET_GAME_DRIVE=0` opt-out, the three-appid helper-path list (it shares the `run_process` port), and the maintainer smoke. Without those gates, it is P1.

Moved out of P0: the lsteamclient cherry-picks go to P1 as a ride-along.

First P1 to run: the P1-3 step-1 spike with the interpose check. Its result decides the entitlement question and the overlay rows of A3 and A4.