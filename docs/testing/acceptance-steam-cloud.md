# Steam Cloud acceptance

Spec: `docs/superpowers/specs/2026-10-11-macneutron-steam-cloud-design.md`. Plan:
`docs/superpowers/plans/2026-10-11-macneutron-steam-cloud.md`.

The maintainer runs every step on their own Mac. SteamIDs and account IDs are never recorded here: every command that
prints log lines or paths pipes them through `R` (below). This copies the redaction used by the 2026-10-11 probes.

**Steam's rule that shapes everything here:** any watched file that is missing at the `up,AC Exit` pass is deleted from
the cloud. So never rename or move anything under `compatdata/<id>/pfx` by hand while a game runs, and don't play
Palworld until phase A step 8's backup exists.

## Setup (one shell, used by every step)

```bash
S="$HOME/Library/Application Support/Steam"
T="$HOME/Library/Application Support/MacNeutron/compatibilitytools.d/macneutron"
P="$S/steamapps/compatdata/1623730/pfx"
B="$HOME/Desktop/steam-cloud-backup"
R() { sed -E -e "s#$HOME#~#g" -e 's/7656119[0-9]+/<SID>/g' -e 's/\[U:1:[0-9]+\]/[U:1:<id>]/g' \
  -e 's#userdata/[0-9]+#userdata/<id>#g' -e 's/(for user|user|account ID|previous user) [0-9]{3,}/\1 <id>/g' \
  -e 's/"[0-9]{6,12}"/"<num>"/g'; }
```

## Phase A: Palworld only, before the new build (checks D1 and the core claim)

1. Quit MacNeutron.app (menu bar → Quit) and Steam (Steam → Quit Steam). Keep MacNeutron.app closed until phase B:
   - each time Steam quits, the running app syncs the full plan, which would put Palworld back on the old tool;
   - at start-up, the current build rewrites `compatibilitytool.vdf` without the test entry.

   Check: `pgrep -x steam_osx; pgrep -x MacNeutron` prints nothing.
2. Back up the config files and the saves you made on the Mac. Do this right before step 3: it is the last moment no
   resolved sync has seen them (spec §4.6).
   ```bash
   mkdir -p "$B" && cp -c "$S/config/config.vdf" "$T/compatibilitytool.vdf" "$S/logs/cloud_log.txt" "$B/"
   cp -c -R "$P/drive_c/users/$USER/AppData/Local/Pal" "$B/pal-mac-made"
   ```
3. Register the second name from the same folder. This is the file the new build writes, except the test display
   name:
   ```bash
   cat > "$T/compatibilitytool.vdf" <<'EOF'
   "compatibilitytools"
   {
     "compat_tools"
     {
       "macneutron-proton"
       {
         "install_path" "."
         "display_name" "MacNeutron (cloud test)"
         "from_oslist"  "windows"
         "to_oslist"    "linux"
       }
       "macneutron"
       {
         "install_path" "."
         "display_name" "MacNeutron"
         "from_oslist"  "windows"
         "to_oslist"    "linux"
       }
     }
   }
   EOF
   ```
4. Start Steam (`open -a Steam`) and wait until the library shows. Check:
   ```bash
   grep -n 'Registering tool macneutron' "$S/logs/compat_log.txt" | tail -3
   ```
   - **Pass:** the newest lines include both `Registering tool macneutron-proton,` and `Registering tool macneutron,`
     (and `macneutron-native`).
   - **Fail:** there is no `macneutron-proton` line. Stop and back out (below). D1 then needs its fallback, a second
     tool folder.
5. Palworld → Properties → Compatibility: force "MacNeutron (cloud test)". **Don't press Play.** If the
   Compatibility tab isn't there: quit Steam, change the `"name"` in Palworld's `"1623730"` block of
   `$S/config/config.vdf` to `macneutron-proton`, then start Steam.
6. Note where the log is now, then restart Steam and wait two minutes. **Don't press Play.**
   ```bash
   N=$(wc -l < "$S/logs/cloud_log.txt"); echo "$N"
   ```
   Then quit Steam, run `open -a Steam`, and wait.
7. Check the new session:
   ```bash
   tail -n +$((N+1)) "$S/logs/cloud_log.txt" | grep -E 'WinAppDataLocal|Pal/Saved|Download' | R | tail -40
   ```
   - **Pass:** no new `Unable to resolve path with root WinAppDataLocal` lines, and download lines for
     `Pal/Saved/SaveGames/…`.
   - **Resolve errors gone but nothing downloads:** the likely cause is Palworld's Windows-only `platformstosync2`
     under the linux platform override. Send the redacted lines; don't change anything else.
8. List what arrived, then back up the whole profile tree (an APFS clone, so it's quick and small):
   ```bash
   find "$P/drive_c/users" -maxdepth 8 -path '*Pal*' | sed "s#$P/##" | R | head -60
   cp -c -R "$P/drive_c/users" "$B/palworld-users"
   ```
   Expected:
   - Steam's download under `drive_c/users/steamuser/Local Settings/Application Data/Pal/Saved/SaveGames/<SID>/…`;
   - your Mac-made saves under `drive_c/users/<you>/AppData/Local/Pal/…`.

   Then list the files both sides have (these are the step B5 conflict), with names only and IDs hidden:
   ```bash
   L="$P/drive_c/users/$USER/AppData/Local/Pal/Saved/SaveGames"
   C="$P/drive_c/users/steamuser/Local Settings/Application Data/Pal/Saved/SaveGames"
   comm -12 <(cd "$L" && find . -type f | sort) <(cd "$C" && find . -type f | sort) | R
   ```
   Expect account-level files such as `./<SID>/GlobalPalStorage.sav`; world folders differ by GUID. For each file
   listed, decide now which version you want to keep.
9. Stop here until phase B. Don't play Palworld, and don't reopen the current MacNeutron.app.

**Back out of phase A:**
1. Set Palworld back to "MacNeutron" in Steam's Compatibility tab (or put the old name back in `config.vdf` with Steam
   closed).
2. Quit Steam.
3. Run `cp -c "$B/compatibilitytool.vdf" "$T/"`.
4. Start Steam.

The downloaded files stay in `steamuser`. Steam stops watching them once the path no longer resolves, and nothing is
deleted.

## Phase B: the new build

1. Build and install MacNeutron.app (`make app`, then replace the installed app with `build/MacNeutron.app`), and
   open it.
   - Its start-up install rewrites `compatibilitytool.vdf` with both names, displayed as "MacNeutron" and
     "MacNeutron (old)": it keeps the old name because the file still has it.
   - Check: `grep -c '"macneutron"$' "$T/compatibilitytool.vdf"` gives 1.
2. Note the cloud log position again: `N=$(wc -l < "$S/logs/cloud_log.txt")`. Then back up every prefix's `users`
   folder. The next step links them all, and Steam's first resolved pass then sees the Mac's saves:
   ```bash
   for u in "$S"/steamapps/compatdata/*/pfx/drive_c/users; do
     id=${u#"$S/steamapps/compatdata/"}; id=${id%%/*}
     mkdir -p "$B/users-$id" && cp -c -R "$u" "$B/users-$id/"
   done
   ```
   Do the same for each extra library in `$S/steamapps/libraryfolders.vdf`.
3. In MacNeutron's Games window, "Restart Steam to apply N changes" → **Restart Steam**. Quitting Steam yourself works
   too: the app moves the mappings when Steam quits. After Steam is back, check:
   ```bash
   grep -c '"name"[[:space:]]*"macneutron"$' "$S/config/config.vdf"   # 0: every mapping moved
   grep -c '"macneutron"$' "$T/compatibilitytool.vdf"                 # 0: the old name is gone
   grep -n 'Registering tool macneutron' "$S/logs/compat_log.txt" | tail -2
   ls -la "$P/drive_c/users" | R
   ```
   Expected:
   - only `macneutron-proton` and `macneutron-native` are registered;
   - Palworld's `steamuser` is still a real folder: the pass that links prefixes while Steam is closed never merges;
   - other games' prefixes have `steamuser -> <you>`.
4. Before playing anything, check what Steam's first resolved pass did for every Windows game:
   ```bash
   tail -n +$((N+1)) "$S/logs/cloud_log.txt" | grep -iE 'delet|conflict|Unable to resolve' | R | tail -40
   ```
   Expected: no delete lines. Send anything unexpected before going on.
5. Launch Palworld once, from Steam. Right after it starts:
   ```bash
   grep -n 'steamuser\|Steam Cloud' "$HOME/Library/Logs/MacNeutron/launcher.log" | R | tail -5
   ```
   - **Merged:** `note: merged steamuser into <you> (N files); the original is now steamuser BACKUP`. Palworld lists
     both the cloud worlds and the local ones.
   - **Conflict** (expected when step A8 listed files): `note: kept steamuser: N of its files differ …`, plus a
     notification. Nothing was changed, and the game runs on the Mac's saves only. Quit the game.
     - For each listed file, copy the version you chose over the other one, so both are identical. Both are already in
       `$B`. For example, to keep the cloud's `GlobalPalStorage.sav`:
       ```bash
       cp -p "$C/<SID>/GlobalPalStorage.sav" "$L/<SID>/GlobalPalStorage.sav"
       ```
       (Type the real folder name. To keep the Mac's version instead, copy the other way.)
     - Never delete or move a file out of `$C`: Steam would take it as deleted on this Mac and delete the cloud copy.
     - Launch again: the merge goes through, and Palworld lists the Mac's worlds and the cloud's.
   - **Steam shows a "Cloud Sync Conflict" dialog instead:** close it without choosing (the launch is cancelled) and
     send the redacted cloud log. Choosing "local" or "cloud" overwrites the other side's colliding files (spec §4.6).
6. Play briefly, save, and quit. Then check the exit pass:
   ```bash
   tail -n +$((N+1)) "$S/logs/cloud_log.txt" | grep -iE 'AC Launch|Watching|AC Exit|Upload|delet' | R | tail -20
   ```
   Expected:
   - at launch, "Watching" shows more than 0 files;
   - the exit pass uploads the changed saves;
   - there are no delete lines for `Pal/Saved`.

**Back out of phase B:** there is no automatic way back, because reinstalling the previous build would leave every
mapping naming `macneutron-proton`, a tool that build doesn't register. Ask first.

## Results

| Date | Step | Result |
|---|---|---|
| | A4 two names registered | |
| | A7 resolved and downloaded | |
| | B3 mappings moved, old name dropped | |
| | B5 merge or conflict | |
| | B6 exit pass | |
