# Acceptance: Valve's signed steamclient64.dll by default (Proton parity P0-1)

Spec: `docs/superpowers/specs/2026-10-12-macneutron-valve-steamclient-design.md`, amended by the parity spec's Part A
item 1 (`docs/superpowers/specs/2026-10-12-macneutron-proton-parity-design.md`). Plan:
`docs/superpowers/plans/2026-10-12-macneutron-valve-steamclient.md`.

## What changed

- **Release guard (A7).** `release/release.sh` refuses an app holding any of Valve's Windows client files, by name and in
  any letter case: `steamclient*`, `GameOverlayRenderer*`, `Steam.dll`, `SteamService*`, `tier0_s*`, `vstdlib_s*`,
  `gameoverlayui*` and `SteamOverlayVulkanLayer*`. Our own `lsteamclient` and `steam.exe` pass. Commit 625fa93.
- **Runtime marker.** `wine-arm64/bundle.sh` writes `wine.app/Contents/Resources/genuine-steamclient` when the Wine tree
  it bundles has patch 0036. `ToolLayout.supportsGenuineSteamclient` reads it. `ToolLayout.valveCache` is
  `~/Library/Application Support/MacNeutron/Valve`. `wine-arm64/check.sh steamclient` asserts the marker. Commit 7c32e31.
- **ValveClient.** It fetches `https://client-update.steamstatic.com/steam_client_win64`, then the `bins_win64` package
  it names, through an ephemeral `URLSession`.
  - It checks the package's size and SHA-256 against the manifest.
  - It extracts Proton's six legacycompat files flat with `unzip -j`, and checks each one's PE machine and certificate
    table.
  - It installs them atomically in `Valve/<version>/` with a `source.txt`, then prunes older versions and stopped
    installs.
  - Manifest fields are checked before any path or URL is built from them.
  - Commit ccb9e09.
- **Launcher.** The prefix's `steamclient64.dll` is chosen in this order:
  1. `MACNEUTRON_STEAMCLIENT64=<path>`, or `=builtin` for lsteamclient;
  2. the cached Valve DLL when the runtime has the marker;
  3. lsteamclient.

  launcher.log names the choice and the reason. Commit 78f5198.
- **App.** After each successful runtime install, on a runtime with the marker, the app checks Valve's manifest once per
  app version, without asking. It downloads only when `bins_win64`'s `sha2` changed or nothing is cached. Settings →
  "Steam components from Valve" shows the version, with Download again and Remove. Remove sticks until Download again
  is pressed. A failure shows only in that row. Commit e5aeb43.

## Gates (2026-10-12)

| Gate | Result |
|---|---|
| `swift test` | 318 tests passed (290 before P0-1) |
| `sh release/release.sh --self-test` | `PASS release self-test`, with 12 new Valve-file rows |
| `sh wine-arm64/check.sh steamclient` (rebuilt runtime, marker present) | `PASS steamclient`. Before the rebuild: `FAIL steamclient: wine.app has no genuine-steamclient marker …` |
| `make bridge-check` | all passed (27 `ok`), SMITE 2's bridge path unchanged |
| `make app APP=build/dev/MacNeutron.app` | built; its `wine.app` carries `genuine-steamclient` |

Each task's RED was recorded first:
- release self-test: 12 rows failed (no `check_no_valve`).
- `PathsTests`: compile failure.
- `ValveClientTests` and `PEImageTests`: compile failure.
- `PrefixManagerTests`: 5 rows failed. The builtin row threw, because `builtin` was read as a path.
- `AppModelTests`: compile failure.

## Final review (2026-10-12)

Four Opus reviewers covered spec compliance, correctness and concurrency, security and the never-ship rule, and scripts
and test honesty. An Opus skeptic then tried to refute each finding.
- Nothing Critical or Important was confirmed.
- Six Minor findings survived.
- One of them was graded up by effect and fixed: a lasting failure in Valve's own files, such as a renamed member. The
  app starts at every login, so it would have fetched the 64 MB package again at every start with no way to stop it.
  - Fix: `updateIfNeeded` now records such a failure for the app version and retries at the next app version or on
    Download again. Network failures are still retried at the next start.
  - Rows: `aContentFailureIsNotDownloadedAgainUntilTheNextAppVersion` (RED, then GREEN) and
    `aNetworkFailureIsRetriedAtTheNextStart`.
  - `swift test`: 320 passed.
- Deferred, as Minor:
  - a `.staging-*` folder from an interrupted Download again stays until the next real install (disk only);
  - `check.sh` falls back to the cache when an explicit `VALVE_STEAMCLIENT64` is missing;
  - the offline app test doesn't pin the error text.

## Maintainer rows (build/dev/MacNeutron.app)

Session of 2026-10-12, on runtime `dev (48329b3d1c02)`, the dev app's `wine.app`. Verdict: "Tested Smite2 and
Palworld, both seems to be working fine".

1. **Silent download.** After the dev app opened, the first game launch (SMITE 2, 10:52Z) logged
   `steamclient64.dll from Valve's client 1788652215 (~/Library/Application Support/MacNeutron/Valve/1788652215/…)`.
   No question was asked. That matches the manifest version of the 2026-10-11 spike. The folder listing and the
   Settings row were not checked separately. — **passed** (by launcher.log)
2. **Palworld without the launch option.**
   - The first launch (10:54Z) still had `MACNEUTRON_STEAMCLIENT64` set to `build/valve`. The override won, as
     designed. The game log has no `Application load error`, and its seven `not supported` lines are Streamline's
     plugin notes and a Wine thread-name note.
   - The second launch (10:55Z to 10:58Z, `exit=0`) had no option. It logged `steamclient64.dll from Valve's client
     1788652215`, and the maintainer reports it working.
   - **passed**
3. **SMITE 2 with Valve's DLL in its prefix:** `exit=0`, working. — **passed**
4. **Remove, then Download again.** — *not run*
5. **Steam's mappings** (`config.vdf`, counted by tool name only): 73 `macneutron-proton`, 0 `macneutron`, 48
   `macneutron-native`. Before, the palfix app had left 73 on `macneutron` and 0 on `macneutron-proton`. The tool-name
   regression is fixed. — **passed**
