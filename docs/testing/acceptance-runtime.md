# Runtime acceptance test (sub-project 1)

Historical: the Rosetta runtime was removed in 0.1.0; reproduce with the frozen reference (`tools/freeze-rosetta-reference.sh`).

Manual. Proves a Windows-only game installs and runs from native macOS Steam's Play button
through `macneutron`, before the menu-bar app (sub-project 3) automates Steam Play mode.

**Warning:** in Steam Play mode, Steam unmounts installed games that have no mapping for the
active platform: their files are deleted and redownloaded later. Step 2 hides every installed
game from Steam first. Do not skip it.

## 1. Install

```sh
make build
target/release/macneutron install-runtime
target/release/macneutron import-gptk "/Volumes/<GPTK volume>"   # optional, enables d3dmetal
```

## 2. Hide installed games from Steam

Quit Steam (Steam > Quit Steam), then:

```sh
S="$HOME/Library/Application Support/Steam"
mkdir -p "$HOME/macneutron-hidden-manifests"
mv "$S"/steamapps/appmanifest_*.acf "$HOME/macneutron-hidden-manifests/"
```

## 3. Enable Steam Play mode by hand

```sh
echo '@sSteamCmdForcePlatformType linux' > "$S/Steam.AppBundle/Steam/Contents/MacOS/steam_dev.cfg"
open -a Steam --env "STEAM_EXTRA_COMPAT_TOOLS_PATHS=$HOME/Library/Application Support/MacNeutron/compatibilitytools.d/macneutron"
```

Check: `grep macneutron "$S/logs/compat_log.txt"` shows `Registering tool macneutron` and
`Loaded manifest for tool macneutron`, and no `Ignoring tool macneutron`.

## 4. Pick the acceptance game

A free, Windows-only D3D11 game. In its Steam Properties > Compatibility, force "MacNeutron",
then install it. Check it does not need the Steam API (that needs sub-project 2's bridge):

```sh
llvm-objdump -p "$S/steamapps/common/<Game>/<Game>.exe" | grep -i "DLL Name"
```

If `steam_api64.dll` is listed, uninstall and pick another game. Record the choice below.

## 5. Run

macOS Steam runs launch options without a shell: always start them with `/usr/bin/env`.


| Run | Launch options | Pass when |
|---|---|---|
| A | (none; d3dmetal if GPTK imported, else dxmt) | Main menu renders; Steam shows the game as running |
| B | `/usr/bin/env MACNEUTRON_GRAPHICS=dxvk %command%` | Same, on the other backend (DXMT is Run A's default without GPTK) |
| C | `/usr/bin/env MACNEUTRON_LOG=1 %command%` | `~/Library/Logs/MacNeutron/steam-<appid>.log` has the environment and Wine output |
| D | (none), then press Stop in Steam | Game closes; Steam stops showing it as running |

After each run, `~/Library/Logs/MacNeutron/launcher.log` has a `verb=waitforexitandrun` line
with the expected backend.

## 6. Revert

1. Uninstall the acceptance game in Steam, then quit Steam.
2. Restore everything:
   ```sh
   rm "$S/Steam.AppBundle/Steam/Contents/MacOS/steam_dev.cfg"
   mv "$HOME/macneutron-hidden-manifests"/appmanifest_*.acf "$S/steamapps/"
   rmdir "$HOME/macneutron-hidden-manifests"
   open -a Steam
   ```
3. Check `$S/logs/content_log.txt` shows no download for the restored games.

## Results

| Date | Game (appid) | GPTK | Run A | Run B | Run C | Run D | Notes |
|---|---|---|---|---|---|---|---|
| 2026-09-27 | Cats (2977660), Unity 2022.3 IL2CPP | none | ✅ DXMT: D3D 11.0 [11.1] on Apple M5 Pro, menu renders | ✅ DXVK after fix `490897a` (first try failed: DXMT's leftover dxgi) | ✅ log written via `/usr/bin/env` | ✅ game quits cleanly (exit 0); on DXVK after a delay of roughly 30 s | **Not a qualifying game**: ships Steamworks.NET (`SteamAPI_Init` fails, game keeps running). Found: launch options need `/usr/bin/env`; uninstall leaves `compatdata/<appid>` (590 MB) and `<Game>.dxvk-cache` behind |
