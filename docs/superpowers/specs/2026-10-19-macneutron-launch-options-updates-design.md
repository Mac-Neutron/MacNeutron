# Launch options in the app, and the app's own updates

Status: design approved in conversation 2026-10-19; this spec awaits the maintainer's review.

## 1. Goal

Two things a player now does by hand move into MacNeutron.app:

- **Launch options.** Variables and game arguments that have no control in the Games window are typed there, per
  game, instead of in Steam's Launch Options box (where a line without `/usr/bin/env` stops the game with "OS Error
  260"). The window also shows the line Steam has for the game, read-only, and says when it is broken or overrides
  the app.
- **Updates.** The app finds a newer release on GitHub, and after the player says yes, downloads, verifies and
  installs it over itself and starts again.

Done when:
- `DXMT_D3D12_SM6=0 -dx12` typed for a game reaches its launch as a variable and an argument, with no Steam launch
  options set; a line pasted from the README (`/usr/bin/env … %command% …`) works the same.
- A game whose Steam line is `MACNEUTRON_LOG=1 %command%` shows the "can't start" warning; one whose Steam line sets a
  variable the app also sets shows which.
- A release build of MacNeutron offers the next release of its own channel and installs it; a `make app` build offers
  nothing; a tampered or differently signed download is refused and nothing changes.

## 2. Decisions (maintainer, 2026-10-19)

| Question | Decision |
|---|---|
| Write Steam's LaunchOptions? | No. Games start with Steam's box empty; the app's field is MacNeutron's, applied by the launcher. Steam's files stay unwritten. |
| Show Steam's own line | Yes, read-only, with warnings (§4). |
| Update mechanism | Built in (GitHub Releases), not Sparkle: no new dependency, no appcast to publish. |
| Update behaviour | Check at launch and daily; ask; install and relaunch on the player's yes. |

Facts the design rests on (read in the code, 2026-10-19):
- The launcher merges the Games window's settings under the environment Steam gives it (`Launcher.withSettings`):
  Steam's launch options win.
- A VM run passes the guest only `MicroVM.passed` (11 variables: `MACNEUTRON_LOG`, `MACNEUTRON_DLSS`, `WINEDEBUG`,
  `DXMT_STATS`, …). Other variables reach the host side only (native DXMT under Direct3D forwarding reads
  `macneutron-vm`'s environment). Game arguments reach the guest (`set --`). This is already true of Steam launch
  options; the field inherits it.
- No game runs from inside MacNeutron.app: the app copies the runtime and the VM's tools out of it at start, and those
  installs already defer while a game runs. Replacing the app while a game runs is safe.
- A release's assets are `MacNeutron-<V>.zip` (made by `ditto -c -k --norsrc --keepParent`) and `SHA256SUMS`; a p
  release is `vX.Y.Zp` with `--latest=false`. `make app` copies `App/Info.plist` as is (version 0.1.0) and signs ad hoc.

## 3. The launch options field

- `GameSettings.launchOptions: String?` (nil: none), saved in `games/<appid>.json` like the other settings.
- `LaunchOptions.parse(_:) -> (environment: [String: String], arguments: [String])` in MacNeutronCore:
  split on whitespace; drop a first word `/usr/bin/env` and every `%command%`; the leading `NAME=value` words
  (`NAME` a shell name) are variables, a later one of the same name winning; every word from the first other one on is
  a game argument, in order. No quoting: a value with a space can't be typed (`ponytail:` comment; add quotes when
  someone needs a path with spaces).
- Order, lowest first: the Games window's controls, the field's variables, Steam's launch options.
  `GameSettings.environment` puts the field's variables over the controls' (one merge); the launcher's existing merge
  keeps Steam's on top.
- The field's arguments go after the game's own (`request.arguments`), for `waitforexitandrun` only: Steam starts
  the game with it, and its install scripts start redistributable installers with `run`. The launcher loads the
  game's settings once and takes both from them.
- Games window: a "Launch options" text field in the selected game's panel, saved on Return and when it loses focus.
  Its help says that in the game VM only the variables MacNeutron passes to the VM reach the game (its logging, DLSS
  and a few DXMT ones), and DXMT's reach its renderer on the Mac.

## 4. Steam's own line, read-only

- `SteamLocation.localConfig: URL?`: `config/loginusers.vdf`'s user with `"MostRecent" "1"`, account ID = the
  SteamID64's low 32 bits, `userdata/<id>/config/localconfig.vdf`; with no such user and exactly one `userdata`
  folder, that one; otherwise nil. The ID is used for the path only: never logged or shown.
- `LaunchOptions.steamLines(localConfig:) -> [UInt32: String]`: each app's non-empty
  `UserLocalConfigStore/Software/Valve/Steam/apps/<appid>/LaunchOptions`, parsed with `KeyValues`. Read in `refresh()`'s
  snapshot, off the main thread. Steam saves the file when it quits (and at times while it runs), so an edit made in
  Steam can show late; nothing here writes it.
- `LaunchOptions.problems(steamLine:appEnvironment:) -> [String]`, shown under the field when the game has a Steam
  line (the line itself is shown too, selectable):
  - its first word is `NAME=value`: "Steam can't start the game with this line: begin it with /usr/bin/env";
  - it sets variables the app sets for this game (`settings.environment`'s keys, the field's included): "Steam's
    launch options override: X, Y".

## 5. The updater

`Sources/MacNeutronCore/Updater.swift`:

- **Which release.** `GET https://api.github.com/repos/chadouming/MacNeutron/releases?per_page=30` (ephemeral session;
  unauthenticated, 60 requests an hour is plenty for a daily check). A release counts if it is neither a draft nor a
  prerelease, its tag is `v` + a version of the running app's channel (`X.Y.Z` for a normal build, `X.Y.Zp` for a
  performance build), and it has the assets `MacNeutron-<V>.zip` and `SHA256SUMS`. The highest such version, compared as numbers
  (0.10.0 > 0.9.0), is offered if it is above the running app's `CFBundleShortVersionString`. A pure
  `pick(json:current:)` does this, for tests.
- **Release builds only.** The updater is on only when the running app is signed with a Developer ID team
  (`CodeIdentity` gains `team(of:)`, `kSecCodeInfoTeamIdentifier`). A `make app` build is ad hoc signed: no team, no
  updater.
- **Install**, all before the running app is touched:
  1. Refuse when the app is translocated (`/AppTranslocation/` in its path) or its folder isn't writable: "Move
     MacNeutron to Applications to update it", with the release page to download by hand.
  2. A work folder on the app's volume (`FileManager.url(for: .itemReplacementDirectory, …, appropriateFor: app)`).
  3. Download the zip and `SHA256SUMS`; the zip must match its line (`VMDownloads.matches`).
  4. `ditto -x -k` it into the work folder.
  5. Verify the new `MacNeutron.app`: `SecStaticCodeCheckValidityWithErrors` with strict, all-architectures and
     nested-code checks against `anchor apple generic and identifier "io.github.chadouming.MacNeutron" and certificate
     leaf[subject.OU] = "<the running app's team>"`; its `CFBundleShortVersionString` is the release's; Gatekeeper
     accepts it (`spctl --assess --type exec`, release.sh's R2 check: Developer ID and notarization).
  6. Swap: rename the running app into the work folder, rename the new one into its place (on failure, rename the old
     one back).
  7. Start a detached `/bin/sh` that waits for this pid to exit, removes the work folder and runs `/usr/bin/open` on
     the app; then quit.

  Any failure before step 6 leaves the installed app as it was and shows the error. Nothing checks for a running game
  (§2); the relaunched app's runtime and VM installs defer as they do today, and "Restart Steam" appears if the tools
  changed under Steam.
- **When.** Settings' "Check for updates automatically" (default on, `UserDefaults`) checks at launch, then hourly
  looks whether 24 hours have passed since the last check (`UserDefaults`). The update state is its own model
  (`UpdateModel`), next to `AppModel`. A version found for the first time
  posts one notification (`AppleScriptNotifier`, as the launcher's); the notified version is remembered so it isn't
  repeated.
- **UI.** The menu shows "Update to X.Y.Z…" while one is available; it opens Settings. Settings' "Updates" row: the
  automatic toggle, "Check now", and when one is available "MacNeutron X.Y.Z is available" with "Release notes" (its
  GitHub page) and "Install and relaunch"; a status line says what is downloading, verifying, or why it failed.
  Install is disabled while another Steam action (`busy`) runs.
- **release.sh is unchanged:** the asset names and `SHA256SUMS` it publishes are what the updater reads.

## 6. Testing

Unit tests (`make test`), none touching the network, Steam's files or MacNeutron's own folders:
- `LaunchOptions.parse`: plain variables, arguments, a pasted `/usr/bin/env … %command% …` line, a repeated name, an
  empty line.
- `GameSettings.environment` with the field over a control; the launcher's merge order (field under Steam) and its
  arguments for `waitforexitandrun` but not for `run`, with the existing launcher test fakes.
- `SteamLocation.localConfig` and `steamLines` on a temp Steam folder (fake IDs); `problems` for the two warnings.
- `Updater.pick` on a recorded-shape JSON: channel filtering, drafts and prereleases, a missing zip, numeric order,
  nothing newer.
- Verification refuses an ad hoc signed bundle and one with another version; the swap and its roll-back on temp
  folders.

The end-to-end update (a real release over a real installed app) is the maintainer's check: the agent never opens
MacNeutron.app.

## 7. Not in this work

- Writing Steam's LaunchOptions or any Steam file.
- Passing more variables into the VM guest (`MicroVM.passed`): its own change.
- Quoted values in the field.
- Delta updates, downgrades, or switching between the normal and performance channels from the app.
