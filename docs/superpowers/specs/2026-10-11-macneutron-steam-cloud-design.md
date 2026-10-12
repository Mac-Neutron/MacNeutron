# MacNeutron: Steam Cloud (Auto-Cloud) for Windows games

Status: approved by the maintainer (2026-10-11), D1–D6 as recommended. D1 still hangs on protocol step A4.

## 1. Goal

Steam Auto-Cloud syncs Windows games run through MacNeutron, in both directions, without ever costing a player a save.
Today nothing syncs, and that is the only reason Palworld's 139 cloud saves (made on another machine) are still intact.

## 2. Facts this design rests on (verified 2026-10-11, client build 1788652215)

- macOS Steam's steamclient.dylib maps Auto-Cloud's Windows roots into a tool's prefix only when the tool's internal name
  contains "proton" (case-insensitive). Names containing "arm64" are ignored as tools. The roots use XP-era names:
  WinAppDataLocal → `pfx/drive_c/users/steamuser/Local Settings/Application Data`, WinAppDataRoaming → `…/Application
  Data`, WinMyDocuments → `…/My Documents`, plus `…/Saved Games`, `…/AppData/LocalLow`, `pfx/drive_c/ProgramData`.
- Our Windows tool is `macneutron`, so Palworld (1623730) logs "Unable to resolve path with root WinAppDataLocal" and its
  exit pass skips the rule. Palworld's 139 cache entries are all stubs (size 0, SHA zeros, never stored on this Mac),
  so once the path resolves, Steam should download them, not delete them.
- A matching name also makes Steam set `STEAM_COMPAT_PROTON=1` in the tool's environment. steamclient.dylib (which our
  bridge loads inside the game) has a predicate that reads it, with `STEAM_COMPAT_PROTON_SUPPRESS` as Valve's off
  switch. What that predicate gates is unknown.
- Our profile is `drive_c/users/$USER` with Vista names. Wine links its `Documents` (and Desktop, Music, …) to the
  Mac's own folders.
- Steam deletes from the cloud every watched file that is missing at the `up,AC Exit` pass, and Steam can create
  `compatdata/<id>/pfx/…` (with cloud files in it) before our tool ever runs.

## 3. Decisions for the maintainer

| # | Question | Recommendation |
|---|---|---|
| D1 | How to rename | Keep the folder `compatibilitytools.d/macneutron` and its bundle link. `compatibilitytool.vdf` lists two tools from that folder: `macneutron-proton` (display "MacNeutron") and, during the move, `macneutron` (display "MacNeutron (old)"). This depends on Steam registering two tools from one vdf, which protocol step A4 checks. If it doesn't, the fallback is a second folder `macneutron-proton` with its own link, removed the same way. |
| D2 | A file that exists in both Steam's `steamuser` tree and our profile, with different content | Leave both in place, don't link, and notify. The game plays on the Mac's saves, Steam's files stay untouched (so no delete and no upload), and the player decides. The alternative, "cloud copy wins and the local copy goes to a backup folder", also keeps everything, but the game silently switches saves. |
| D3 | Linking existing prefixes ahead of time | Yes. In the Steam-closed sync that moves the mappings, add the missing links to every prefix that has a real profile and no `steamuser` folder. Steam's first resolved pass then sees the Mac's saves where Proton prefixes keep them, and handles any conflict its own way. Without this, every existing prefix goes through a merge at its first launch. |
| D4 | Merging at launch, inside the window the safety rule protects | Allowed only as described in §4.2: clone, check, then one atomic swap, so every watched path holds the same bytes at every moment. The stricter reading (no merge at launch) means a newly installed game's first session doesn't see its cloud saves. |
| D5 | `STEAM_COMPAT_PROTON=1` | The launcher removes it, so the game's environment stays exactly as it is today. |
| D6 | WinMyDocuments | Accept that these files land in the Mac's `~/Documents` (through Wine's link). Games already write there, and Timberborn's Mac version keeps its saves in `~/Documents/Timberborn` too. |

## 4. Design

### 4.1 Rename and mapping migration

- `MappingPlanner.runtimeTool = "macneutron-proton"`, and a new `legacyRuntimeTool = "macneutron"`.
  `MappingPlanner.isOurs` becomes the literal `hasPrefix("macneutron")`. Today it is `hasPrefix(runtimeTool)`, so with
  the new name `macneutron-native` entries would stop counting as ours, and `sync` would refuse every plan
  (`planDropsMacGames`). The literal prefix also counts the legacy entries as ours, so `pendingChanges` counts them
  ("Restart Steam to apply N changes") and `merged` replaces them.
- `SteamPlayMode` keeps the folder and link name `macneutron` apart from the Steam tool name. `verify(log:)` accepts
  `Registering tool macneutron-proton,` or `Registering tool macneutron,`, so a Steam session started before the
  update doesn't show as "lost".
- The migration is the existing flow. Whenever Steam quits, or the user clicks Restart Steam, `sync(plan:)` runs
  `applyMappings`, which rewrites every legacy entry. Then (D3) it adds the links to existing prefixes, and finally it
  rewrites `compatibilitytool.vdf` without the legacy entry. That last step is the "old tool removed only after every
  mapping has moved" rule.
- `RuntimeInstaller.writeToolFiles` gets `includeLegacyName: Bool`, default true. The app passes false once
  `config.vdf` names `macneutron` nowhere. Keeping the extra entry is never unsafe; a mapping to a missing tool is.
- `macneutron-native` is unchanged (Timberborn syncs through its Mac root override).

### 4.2 Profile links (`ProfileLinks.apply`, new file)

Called inside `PrefixManager.prepare`'s lock on every launch, after the stamp logic (also on the up-to-date path).
D3's pass calls it too, in a mode that only adds links and never merges.

1. Find the profile: `drive_c/users/$USER` must be a real directory. If `$USER` is `steamuser`, step 3 is skipped.
   (Implementation check: confirm in Wine 11.19's source that the profile name comes from `$USER`.)
2. Inside the profile: `Local Settings/Application Data` → `../AppData/Local`, `Application Data` → `AppData/Roaming`,
   `My Documents` → `Documents` (relative links, as Proton makes them).
3. `drive_c/users/steamuser` → `$USER`.

For each link, depending on what is at the path now:

- **Nothing:** create the parent folders and the link.
- **A link:** leave it if it points to the right place; otherwise replace it (a link holds no data).
- **A real folder (Steam made it):**
  1. Compare the folder's regular files with the same relative paths under the target. Paths are resolved through
     the target's links, so `steamuser/Local Settings/Application Data/Pal/x` is compared with
     `AppData/Local/Pal/x`. Symbolic links inside the source are skipped.
  2. If any file differs: change nothing (D2), log it, and notify.
  3. Otherwise, clone the files the target lacks (APFS clones keep the bytes and the modification time), then compare
     again: every file must now be present and equal.
  4. Create the link at `<name> BACKUP` (` BACKUP 2`, … if taken), then `renamex_np(RENAME_SWAP)` it with the folder.
     After this single atomic step the link sits at the path and the old folder is the backup. Nothing is deleted.
- **A file:** leave it and log.

Step 2 runs before step 3, so the merge of `steamuser` lands in the Vista folders. The log never contains file paths
below the first folder name, because Palworld's save path contains the SteamID. A volume that isn't APFS is
skipped with a log line: without clones or `RENAME_SWAP`, the merge can't be made crash-safe.

### 4.3 Prefix guard

In `prepare`: a `pfx` with no `version` stamp and no `pfx/user.reg` was seeded by Steam. Build into it
(`wineboot -u` in place, then the usual steps), and never rename it to `pfx.rosetta` or carry from it. After the
rename, this is the normal first launch of any newly installed Windows game. Proton commit 1846fe4 handles the same
case.

### 4.4 Environment

`LaunchEnvironment.build` removes `STEAM_COMPAT_PROTON` (D5).

### 4.5 App warnings

- **Leftover data** (`CleanupView`): a line under the title: "These folders hold games' Windows settings and saves.
  Saves that aren't in Steam Cloud exist nowhere else." "Delete selected" opens a confirmation listing the games:
  "Delete N games' data? This can't be undone."
- **Runs as** (`GamesView`): changing a dual-platform game's version first asks: "Steam Cloud keeps this game's saves
  in a different place for each version. Back up your saves before switching." with buttons Switch and Cancel.

## 5. How the hazards are avoided

| Hazard | Handling |
|---|---|
| Watched file missing at the exit pass | The `pfx` is never renamed (§4.3). A link replaces a real folder only after every file in it is present and equal at the target, in one atomic swap (§4.2). A crash before the swap leaves the original folder in place, and a crash after it leaves the link. |
| Local copy silently uploaded over the cloud copy | Differing files block the merge (D2). |
| Mapping to a tool Steam doesn't know | The old name stays registered until no mapping uses it (§4.1). |
| "Leftover data" deleting the only copy of a save | Warning and confirmation (§4.5). |
| Switching a dual-platform game's version | Warning (§4.5). |

## 6. Tests (`make build test`)

- `ProfileLinksTests`, in the style of the fake profiles in `PrefixManagerTests`:
  - an empty profile gets all four links, with the expected relative targets;
  - a seeded `steamuser/Local Settings/Application Data/G/s.sav` ends up at `AppData/Local/G/s.sav`, `steamuser`
    becomes a link, and `steamuser BACKUP` holds the original;
  - a differing file changes nothing on disk; an identical file merges;
  - a second run makes no change;
  - when the profile is named `steamuser`, there is no `steamuser` link.
- `PrefixManagerTests`:
  - a seeded `pfx` is prepared in place, with no `pfx.rosetta` and its files intact;
  - the `rosettaPrefix` fixture writes `user.reg`, so the existing rename tests still exercise the rename path.
- `MappingPlannerTests` and `SteamPlayModeTests`:
  - legacy entries count as ours and are replaced, and native entries still count as ours;
  - `verify` accepts both names;
  - `sync` moves legacy mappings, then drops the legacy vdf entry.
  - The fixtures (`FakeSteam`, `AppModelTests`) move to the new name.
- `RuntimeInstallerTests`: the vdf with and without the legacy entry.
- `LaunchEnvironmentTests`: `STEAM_COMPAT_PROTON` is removed.

## 7. Maintainer protocol (summary; the full text goes to `docs/testing/acceptance-steam-cloud.md`)

**Phase A, before any code changes (checks D1 and the core claim on Palworld only):**

1. Quit MacNeutron.app and keep it closed through phase A: on every Steam quit, it would put Palworld's mapping back.
   Quit Steam. Back up `config/config.vdf` and the tool's `compatibilitytool.vdf`.
2. Add a second entry, `macneutron-proton` (display "MacNeutron (cloud test)"), to
   `MacNeutron/compatibilitytools.d/macneutron/compatibilitytool.vdf`.
3. Start Steam.
4. Check: `compat_log.txt` shows both `Registering tool macneutron,` and `Registering tool macneutron-proton,`. If
   only one appears, stop: D1 needs its fallback.
5. Palworld → Properties → Compatibility → "MacNeutron (cloud test)". If that tab is missing, set the name by hand in
   `config.vdf` while Steam is closed. Don't launch the game.
6. Quit Steam and start it again. Wait a minute. Don't launch the game.
7. Check `cloud_log.txt` from the new session (piped through the probes' redaction): there should be no new "Unable to
   resolve path … WinAppDataLocal" lines, and there should be download lines for `Pal/Saved/SaveGames`. If the
   resolve errors are gone but nothing downloads, the next suspect is Palworld's Windows-only `platformstosync2` under
   the linux platform override; report the redacted lines.
8. With approval, list the names (redacted) under `compatdata/1623730/pfx/drive_c/users`, then back up that whole
   `users` folder (`cp -c -R`). Palworld has run here before, so a local `Pal` tree is expected beside Steam's.
9. Don't play until phase B. Don't reopen the current MacNeutron.app before phase B either: at start-up it rewrites
   `compatibilitytool.vdf` without the test entry while Palworld is still mapped to it. To back out instead, set
   Palworld back to "MacNeutron" in Steam, quit Steam, then restore the backed-up vdf.

**Phase B, after implementation:**

1. Install the build and open MacNeutron.app. Restart Steam from it. Check that `compat_log.txt` registers
   `macneutron-proton`, and that `config.vdf` no longer names `macneutron`.
2. Launch Palworld once. `launcher.log` shows either the merge or the conflict note.
3. Quit the game, then check the exit pass in `cloud_log.txt`: it watches files and shows no delete lines.

## 8. Out of scope

- Games using the Steam Cloud API (`ISteamRemoteStorage`) rather than Auto-Cloud.
- Steam libraries on non-APFS volumes. Their games get no merge, and the log says so.
- Moving "Leftover data" to the Trash instead of deleting it.
