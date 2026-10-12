# MacNeutron: Valve's signed steamclient64.dll in every prefix

Status: draft for the maintainer (2026-10-12). The spike it builds on passed on 2026-10-11: Palworld got past
SteamStub with Wine patch 0036 and `MACNEUTRON_STEAMCLIENT64` (repo 65c748e).

## 1. Goal

Games wrapped in SteamStub DRM (Palworld) start with no launch option. Today they stop with "Application load error
3:0000065432" unless the maintainer passes `MACNEUTRON_STEAMCLIENT64=<path to Valve's DLL>` by hand.

## 2. Facts this design rests on (verified 2026-10-11)

- SteamStub's error 3 is a failed signature check of `steamclient(64).dll`, `Steam.dll` or the exe (PCGamingWiki's
  Steam DRM notes; the number after the colon minus 65432 is `GetLastError()`, 0 here). Our prefixes held an unsigned
  copy of our builtin lsteamclient as `steamclient64.dll`.
- Proton keeps Valve's signed Windows DLLs from Linux Steam's `legacycompat` folder in the prefix. Its Wine maps the
  genuine `steamclient64.dll` and redirects its exports to lsteamclient (ValveSoftware/wine proton_11.0). Wine patch
  0036 does the same on our stack (x64 jumps written RW then restored, DllMain and TLS callbacks skipped, ntdll stubs
  for `tier0_s64`/`vstdlib_s64`). `wine-arm64/check.sh steamclient` covers it, with 40 of 40 exports redirected.
- macOS Steam ships no Windows DLLs: there is no `legacycompat` folder.
- Valve publishes the Windows client openly:
  - Manifest: `https://client-update.steamstatic.com/steam_client_win64`, a KeyValues text file with a `version` and
    one entry per package (`file`, `size`, `sha2`, and `zipvz`/`sha2vz` for an LZMA variant).
  - Package: `bins_win64` (about 64 MB, a plain zip). It holds `steamclient64.dll` (26 MB, x64), `steamclient.dll`
    (x86), `Steam.dll`, `tier0_s64.dll`, `vstdlib_s64.dll` and more.
  - The 2026-10-11 download matched the manifest's SHA-256, and its `steamclient64.dll` is Authenticode-signed by
    "Valve Corp." (DigiCert G4 chain). Wine's WinVerifyTrust accepts it.
- The genuine DLL's code never runs: only its file and headers are read (by SteamStub). Its exports jump to our
  lsteamclient, which talks to macOS Steam. So any recent Valve build works; it need not match the Mac client's
  version.
- With an old runtime (no 0036), a genuine DLL in the prefix fails to load at `tier0_s64.dll`, so the game gets no
  Steam. That is safe, but it breaks Steam for the game, so deployment must depend on the runtime having 0036.

## 3. Decisions for the maintainer

| # | Question | Options | Recommendation |
|---|---|---|---|
| D1 | When to download | (a) when the app installs or updates the runtime; (b) at the first game launch that needs it; (c) both: (a), with (b) as a fallback | **(a)**. The app has UI for progress and errors, and a launch stays fast and offline-safe. Without the DLL, a launch falls back to today's lsteamclient copy and logs a note. |
| D2 | Ask first? | (a) a one-time consent sheet ("Download Steam's Windows components from Valve, about 64 MB"); (b) silent, with a Settings row | **(a)** once, remembered. It is a 64 MB third-party download. Declining keeps today's behaviour, with a Settings button to download later. |
| D3 | What to keep | (a) `steamclient64.dll` only; (b) also `steamclient.dll` (32-bit games, after WoW64 support); (c) also `Steam.dll` | **(a)+(b)**, cached together. `Steam.dll` only if a game proves it needs it. |
| D4 | Refresh | (a) never; (b) on each app update; (c) monthly | **(b)**: re-read the manifest on app update and download only if `bins_win64`'s `sha2` changed. |
| D5 | Opt-out | a launch option `MACNEUTRON_STEAMCLIENT64=builtin`, plus Settings | **Yes**. `=<path>` keeps working, as in the spike. |

## 4. Design (with the recommendations)

1. **ValveClient** (new, MacNeutronCore). It runs only on request, from the app:
   1. Fetches the manifest over HTTPS (URLSession, no cookies, no account data).
   2. Parses it with `KeyValues`.
   3. Downloads `bins_win64`'s `file` to a temporary file and verifies its SHA-256 against `sha2`; on a mismatch it
      discards the file and reports an error.
   4. Extracts `steamclient64.dll` and `steamclient.dll` with `/usr/bin/ditto -x -k` (or `unzip`) into a temporary
      folder.
   5. Checks each file's PE machine (`PEImage`): AMD64 for steamclient64.dll, i386 for steamclient.dll. It also checks
      that a certificate table is present.
   6. Moves the files into `~/Library/Application Support/MacNeutron/Valve/<manifest version>/`, atomically, beside a
      `source.txt` recording the URL, `sha2` and date.
   7. Removes older versions once the new one is in place.
2. **Capability.** The runtime advertises 0036 with a marker file in `wine.app/Contents/Resources`
   (`genuine-steamclient`), written by `wine-arm64/build.sh` once 0036 is in the series. `ToolLayout` exposes it as
   `supportsGenuineSteamclient`.
3. **Deployment** (`PrefixManager.deploySteamBridge`). The prefix's `steamclient64.dll` is chosen in this order:
   1. The override: `MACNEUTRON_STEAMCLIENT64=<path>`, or `=builtin` for lsteamclient.
   2. The cached Valve DLL, if the runtime supports it.
   3. lsteamclient, today's behaviour.

   The launcher log notes which one and why. The existing size-and-mtime copy rule keeps relaunches cheap.
4. **App:**
   - Shows the one-time consent sheet (D2) when it installs or updates a runtime that supports the DLL and none is
     cached.
   - Shows progress and errors.
   - Has a Settings row: "Steam components from Valve: version, Download again, Remove".
5. **Never shipped by us.** Valve's files are never committed, bundled into the app or release zip, or uploaded. The
   download comes from Valve to the user's Mac, as Linux Steam's `legacycompat` does for Proton. `release/release.sh`
   gains a check that no Valve DLL is in the app.

## 5. Tests

- **Swift, no network:**
  - A fake manifest and a fake zip in a temp folder, served through an injected fetcher. They hold tiny PE files with
    the right machines: never Valve's.
  - Rows: SHA mismatch rejected; wrong machine rejected; atomic install; old version pruned; capability gating;
    deployment order (override, cache, builtin); `=builtin`; launcher-log notes; offline launch with no cache falls
    back.
- **Runtime:** `wine-arm64/check.sh steamclient` (exists). Its DLL source becomes the cache when present.
- **Maintainer:**
  - After installing, Palworld starts with no launch option and the launcher log names the cached DLL.
  - SMITE 2 (no SteamStub) still runs.
  - Declining consent keeps today's behaviour.

## 6. Out of scope

- 32-bit games: the `steamclient.dll` path needs WoW64 support (separate spec), and 0036 handles only
  `steamclient64.dll`.
- `GameOverlayRenderer64.dll`, `Steam.dll` and `SteamService.exe`, until a game needs them.
- Steam Cloud and the steam.exe glue (separate work).
