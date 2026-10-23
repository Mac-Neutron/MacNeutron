# A game in the VM uses the saves Steam Cloud syncs: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A game in the VM reads and writes the Mac prefix's profile, `~/Documents` and its own game folder, so Steam Cloud and the VM use the same saves.

**Architecture:** The launcher resolves three Mac folders and passes them to macneutron-vm as writable virtiofs shares in libkrun's simplified permission mode (new patch 0012), plus a `mounts` file in the run's share. mn-runner (root, in the guest) mounts them before handing over to `mn`, or leaves `mounts.failed` and powers off without running the game. run-game.sh runs Wine as `steamuser` with XDG folders naming the Mac's, so Wine keeps the Mac profile's links as they are.

**Tech Stack:** Swift (MacNeutronCore, macneutron-vm), Rust (libkrun fork, patch 0012), POSIX sh (mn-runner, run-game.sh, gates), Swift Testing.

**Spec:** `docs/superpowers/specs/2026-10-23-macneutron-vm-shared-saves-design.md`

## Global Constraints

- The profile is always the folder Steam watches; nothing renames or relinks `compatdata/<appid>/pfx` during a run (spec §5).
- The Mac's files keep their owner and gain no xattrs; files the VM makes are the Mac user's (spec §5).
- Saves on old VM disks are not migrated; `users/mn` stays on its disk, unused (spec §2).
- libkrun changes live in our fork's patch series only; no upstream PR without the maintainer.
- Never print or commit SteamIDs or account names; write the home folder as `~` in committed docs.
- The maintainer opens MacNeutron.app and starts Steam; the executor never does. Ask before any download. Nothing is pushed.
- Launcher tests never touch the real VM folder or the real `~/Documents` (temp folders only).
- One-lane gates per task (the task's own gate), full gates at the end (Task 6).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A game folder with a space, an apostrophe or non-ASCII letters (`SMITE 2`, `Baldur's Gate 3`) stays one path in `--user-share`, in `mounts` and in mn-runner's mount: Task 1 (launcher test) and Task 3 (gate row `odd-name`).
2. `~/Documents` that can't be listed (macOS privacy refused it for Steam, or it's missing): the launch is refused before the boot with a message naming System Settings, never run without it: Task 1.
3. `users/steamuser` is a link whose profile folder was deleted: the target folder is made and the link kept: Task 1.
4. A `mounts.failed` left by an earlier run doesn't refuse the next, good run: Task 1 (the launcher removes it before the run).
5. A library on another drive (`/Volumes/…/steamapps/common`): the game share comes from that library: Task 1.

---

### Task 1: The launcher's shares, `mounts` file and refusals

Needs no VM: the launcher's side, tested with the existing fixture in `Tests/MacNeutronCoreTests/MicroVMTests.swift`.

**Files:**
- Modify: `Sources/MacNeutronCore/MicroVM.swift` (new `UserShare`, `userShares`, `mountsFile`; `"MN_MAC_HOME"` in `passed`)
- Modify: `Sources/MacNeutronCore/Launcher.swift` (`init` gains `home`; `runInMicroVM` at the run-guest.sh call, ~line 298)
- Test: `Tests/MacNeutronCoreTests/MicroVMTests.swift`

**Interfaces:**
- Produces:
  - `struct UserShare: Equatable, Sendable { let tag: String; let host: String; let guest: String }` in MicroVM.swift.
  - `static func userShares(prefix: URL, library: String, guest: String, home: String) throws -> [UserShare]`: in this order `profile` (host = `prefix/drive_c/users/steamuser` resolved through links, made when missing or dangling; guest `/home/mn/pfx/drive_c/users/steamuser`), `documents` (host = guest = `home + "/Documents"`), `game` (host = `library + "/" + <game folder>`, guest = `"/mnt/games/" + <game folder>`, where `<game folder>` is `guest`'s first component after `/mnt/games/`; no game share when there is none).
  - `static func mountsFile(_ shares: [UserShare]) -> String`: one `"\(tag) \(guest)\n"` per share.
  - `enum UserShareError: Error, CustomStringConvertible { case documentsUnreadable(String) }`, description exactly: `MacNeutron can't open your Documents folder (<path>), so a game in the VM couldn't reach its saves there. Allow Steam in System Settings > Privacy & Security > Files and Folders, then launch the game again.`
  - run-guest.sh gets, after `--steam-map …`, `--user-share <tag>=<host>` per share; the run share (`runs/<appid>`) gets `mounts`; the script header gets `MN_MAC_HOME='<home>'`.
  - After the run, a `mounts.failed` in the run share posts `MacNeutron couldn't share this game's saves with the game VM: <its first line>` and logs it.
- `Launcher.init` gains `home: String = NSHomeDirectory()` (last parameter); the fixture passes a temp home with `Documents/` made.

- [ ] **Step 1: Write the failing tests** in `MicroVMTests.swift`. `VMFixture` gains `home: String` (a temp folder with
  `Documents/`, passed as the launcher's `home`), `dataPath: URL` (the env's compatdata folder) and `scripts: Scripts`
  (the one `makeVMFixture` hands the runner):

```swift
@Test func aVMGameGetsItsProfileDocumentsAndFolder() throws {
    let f = try makeVMFixture()
    _ = f.launcher.launch(["waitforexitandrun", game], environment: f.env)
    let call = try #require(f.runner.calls.first)
    let profile = f.dataPath.appending(path: "pfx/drive_c/users/steamuser").path(percentEncoded: false)
    #expect(Array(call.arguments.dropFirst(7)) == [  // after the script, appid, RAM and --steam-map pairs
        "--user-share", "profile=\(profile)",
        "--user-share", "documents=\(f.home)/Documents",
        "--user-share", "game=/Users/someone/Library/Application Support/Steam/steamapps/common/SMITE 2"])
    #expect(try String(contentsOf: f.vm.appending(path: "runs/2437170/mounts"), encoding: .utf8) ==
            "profile /home/mn/pfx/drive_c/users/steamuser\ndocuments \(f.home)/Documents\ngame /mnt/games/SMITE 2\n")
    #expect(f.scripts.all.first?.contains("MN_MAC_HOME='\(f.home)'") == true)
    #expect(FileManager.default.fileExists(atPath: profile))  // made: Steam's and Proton's layout
}
@Test func aLinkedProfileIsSharedAsItsTarget() throws        // users/steamuser -> chad: host is users/chad
@Test func aDanglingProfileLinkGetsItsFolderBack() throws    // link kept, users/chad made
@Test func aGameFolderWithAnApostropheStaysOnePath() throws  // "Baldur's Gate 3": arguments and mounts line whole
@Test func aLibraryOnAnotherDriveSharesThatLibrarysGame() throws  // /Volumes/Games/steamapps/common/X
@Test func documentsThatCantBeListedRefuseTheLaunch() throws // chmod 000 home/Documents: no run-guest.sh call,
                                                             // notification == UserShareError.documentsUnreadable(...).description
@Test func aFailedMountIsToldToThePlayer() throws            // fake run writes mounts.failed "no virtiofs tag game":
                                                             // posted "MacNeutron couldn't share this game's saves with the game VM: no virtiofs tag game"
@Test func anOldMountsFailedDoesNotRefuseTheNextRun() throws // file there before; fake run leaves none: nothing posted
```

Update `everyGameRunsInTheMicroVM`'s exact argument list: the three `--user-share` pairs follow the `--steam-map` pair.

- [ ] **Step 2: Run them to see them fail**

Run: `swift test --filter MicroVMTests`
Expected: build failure (`UserShare`, `home:` don't exist), then after stubbing, the new tests FAIL.

- [ ] **Step 3: Implement** `UserShare`, `userShares`, `mountsFile`, `UserShareError` in MicroVM.swift and the launcher's part of `runInMicroVM`: resolve shares (catch `UserShareError` with `fail(error.description, argv:, notify: true, gameLog:)`), check Documents with `FileManager.contentsOfDirectory` (any error is `documentsUnreadable`), write `mounts`, remove `mounts.failed`, set `environment["MN_MAC_HOME"] = home` before `vm.script`, append the arguments, read `mounts.failed` after the run. Resolve a link with `URL.resolvingSymlinksInPath()`; for a dangling one, make the link's target.

- [ ] **Step 4: Run the tests**

Run: `swift test`
Expected: all pass (462 before this task, plus the new ones).

- [ ] **Step 5: Commit**

```bash
git add Sources/MacNeutronCore/MicroVM.swift Sources/MacNeutronCore/Launcher.swift Tests/MacNeutronCoreTests/MicroVMTests.swift
git commit -m "launcher: a VM game gets its profile, ~/Documents and its own folder as user shares"
```

### Task 2: Restore the build (prerequisite for Tasks 3-6)

`build/` was deleted on 2026-10-23, with the builder VM (`build/eac-vm-work/eacvm/fedora/vm`) inside it. Likely its own session. Ask the maintainer before each download (Fedora 44 Cloud image, Fedora packages, Rust toolchains, llvm-mingw).

**Files:** none in the repo, unless a script turns out to depend on something only the old `build/` had (fix it in the script, separate commit).

- [ ] **Step 1:** The Mac side, in this order: `make wine-arm64` (re-applies the pinned series; check each tree's `build_mode` is `release`, release runbook), `sh eac-vm/libkrun/build.sh`, `sh eac-vm/host/build.sh` (with `MACNEUTRON_SIGN_IDENTITY`), `make app APP=build/MacNeutron.app`.
- [ ] **Step 2:** The builder: a Fedora 44 Cloud (aarch64) VM at `build/eac-vm-work/eacvm/fedora/vm` started by `eac-vm/builder.sh start`, provisioned with `eac-vm/guest/provision.sh`; the pinned sysroot (`eac-vm/sysroot`); FEX (`eac-vm/image/build-fex.sh`).
- [ ] **Step 3:** The guest's images and stages: `eac-vm/image/build.sh` (all images), `eac-vm/image/guest-stages.sh` (dxmt, d3d, wine, forwarder, fex), the share's tools (`eac-vm/guest/build-audio.sh`, `build-input.sh`), `mn-steam-host`. Start the builder with both shares (`VM_SHARES out=…/images,outp=…`).
- [ ] **Step 4: Verify the restored build**

Run: `sh eac-vm/tests/games-xattr.sh && sh eac-vm/tests/modes.sh && sh eac-vm/tests/vm-setup.sh`
Expected: `PASS`, `modes: ok`, `vm-setup: ok`.

- [ ] **Step 5: Baseline bench (the maintainer starts Steam):** `eac-vm/tests/bench-smite2.sh`, SMITE 2's lobby for 2 minutes, once. Record launch time and fps in `.superpowers/sdd/2026-10-23-macneutron-vm-shared-saves/progress.md` for Task 6.

### Task 3: User shares reach the guest (libkrun 0012, macneutron-vm, mn-runner)

**Files:**
- Create: `eac-vm/libkrun/patches/0012-virtio-fs-a-share-with-simplified-permissions.patch` (made with `git format-patch` in `build/eac-vm-work/libkrun-src`, branch `macneutron`, trailer as 0011's)
- Modify: `eac-vm/libkrun/build.sh` (header: what 0012 does)
- Modify: `eac-vm/host/macneutron-vm.swift` (usage comment, `--user-share`, device setup after `share`/`games`, ~line 166)
- Modify: `eac-vm/image/rootfs/usr/libexec/mn-runner`
- Modify: `eac-vm/run-guest.sh` (the `rm -f` of last run's results also removes `mounts.failed`)
- Modify: `eac-vm/image/build.sh` and `eac-vm/image/recipe.py`: the base image gets an empty `/Users` (a mount point; the root is read-only EROFS). Both, so `vm-setup.sh`'s file-for-file comparison holds.
- Create: `eac-vm/tests/user-shares.sh`

**Interfaces:**
- Consumes: Task 1's `--user-share <tag>=<host>` arguments and `mounts` lines `<tag> <guest path>`.
- Produces:
  - libkrun: `FsDevice::new_simplified(tag: &str, host_path: &str)` in `src/libkrun/src/api/device_builders.rs` (`new_inner` gains a `PermissionSemantics` argument; the other constructors pass `LinuxComplete`), exported by ffier as `krun_fs_device_new_simplified`; `include/libkrun.h` gets its declaration if the header isn't generated.
  - macneutron-vm: every `--user-share tag=dir` (repeatable, split at the first `=`) is a `krun_fs_device_new_simplified` device; a `dir` that isn't readable and writable fails with exit 2, `missing share: <dir>`.
  - mn-runner: with `/mnt/share/mounts` present, before `setpriv`: a tmpfs on `/Users`; for each `read -r tag path` line, the path must match `/home/mn/pfx/drive_c/users/*`, `/Users/*` or `/mnt/games/*` and have no `..` component, else refused; `mkdir -p` its mount point (owned by `mn` under `/home/mn` or `/Users`), `mount -t virtiofs "$tag" "$path"`. Beside the `documents` line's path: `Desktop Downloads Music Pictures Movies` as links to `/home/mn/<name>` (made, owned by `mn`). Any failure: the reason to `/mnt/share/mounts.failed` and `run.log`, `sync`, poweroff, no `run.sh`, no `run.status`.

- [ ] **Step 1: Write the gate `eac-vm/tests/user-shares.sh`** (pattern of `games-xattr.sh`: a temp profile, Documents and library on the Mac, a `mounts` file in its own `MN_SHARE`, `--user-share` arguments to run-guest.sh, a guest script run as `mn` that prints `row <name> ok|fail`; the host checks the rest). Rows:
  - `profile-write`: a new file in the profile is on the Mac, owned by `id -u`, `xattr` lists nothing.
  - `mac-file-write`: appending to a file the Mac made (mode 644) works; on the Mac its owner and xattrs are unchanged.
  - `chmod`: `chmod 600` in the guest is mode 600 on the Mac, no xattr.
  - `link-rename`: `ln -s ../x l` reads back `../x` on the Mac; `mv a b` leaves `b` on the Mac.
  - `documents`: a file in `/Users/gate/Documents` is in the Mac's folder.
  - `odd-name`: the game folder `Gate's Game 2` takes a write; another game's folder in the same library refuses one (read-only).
  - `mac-folders`: `/Users/gate/Desktop` is a link to `/home/mn/Desktop`, writable by `mn`.
  - `refused`: runs whose `mounts` names `game /etc/x`, then `profile /home/mn/pfx/drive_c/users/../../x`, then tag `nosuch`: each leaves `mounts.failed` and no marker from `run.sh`.
  Prints `user-shares: ok` or `user-shares: FAIL <rows>`.

- [ ] **Step 2: Run it to see it fail**

Run: `sh eac-vm/tests/user-shares.sh`
Expected: `user-shares: FAIL` (macneutron-vm doesn't know `--user-share`).

- [ ] **Step 3: Implement** patch 0012, macneutron-vm's `--user-share`, the `/Users` mount point in both image builders, mn-runner's mounts and run-guest.sh's `rm`. Rebuild: `sh eac-vm/libkrun/build.sh`, `sh eac-vm/host/build.sh`, `eac-vm/image/build.sh base` in the builder.

- [ ] **Step 4: Run the gates**

Run: `sh eac-vm/tests/user-shares.sh && sh eac-vm/tests/games-xattr.sh && sh eac-vm/tests/vm-setup.sh`
Expected: `user-shares: ok`, `PASS`, `vm-setup: ok`. A failing row that disproves spec §8 (symlinks or renames under simplified mode, a mount over the read-only `games` mount) stops the task: back to the maintainer and the spec.

- [ ] **Step 5: Commit**

```bash
git add eac-vm/libkrun eac-vm/host/macneutron-vm.swift eac-vm/image eac-vm/run-guest.sh eac-vm/tests/user-shares.sh
git commit -m "eac-vm: user shares (libkrun 0012's simplified permissions) mounted by mn-runner before the game"
```

### Task 4: Wine keeps the Mac profile as it is (run-game.sh)

**Files:**
- Modify: `eac-vm/guest/run-game.sh` (after the `export WINEPREFIX=…` line)
- Modify: `eac-vm/tests/user-shares.sh` (Wine rows)

**Interfaces:**
- Consumes: Task 1's `MN_MAC_HOME` header line; Task 3's mounts.
- Produces: with `MN_MAC_HOME` set, run-game.sh exports `USER=steamuser` and writes `$HOME/.config/user-dirs.dirs` with `XDG_DESKTOP_DIR`, `XDG_DOCUMENTS_DIR`, `XDG_DOWNLOAD_DIR`, `XDG_MUSIC_DIR`, `XDG_PICTURES_DIR` = `"$MN_MAC_HOME/<Desktop|Documents|Downloads|Music|Pictures>"` and `XDG_VIDEOS_DIR="$MN_MAC_HOME/Movies"`. Without it (the gates' own runs), nothing changes.

- [ ] **Step 1: Add the Wine rows** to `user-shares.sh`: run run-game.sh with `VERB=run EXE=cmd.exe` and `set -- /c echo %USERPROFILE%`, a fresh state disk (so run-game.sh's `wineboot -u` runs), and a Mac profile laid out as macOS Wine makes it (six absolute links to `/Users/gate/…`, the three XP-name links as in `ProfileLinks.xpLinks`):
  - `profile-name`: the output is `C:\users\steamuser`.
  - `links-unchanged`: `readlink` of every link in the Mac profile is identical before and after.
  - `real-documents-kept`: with `Documents` a real folder holding `My Games/x.sav`, it's still a real folder with that file afterwards.
  - `old-prefix`: on a state disk whose prefix `wineboot -i` made without `USER=steamuser` (`users/mn`), the output is still `C:\users\steamuser`.

- [ ] **Step 2: Run them to see them fail**

Run: `sh eac-vm/tests/user-shares.sh`
Expected: `user-shares: FAIL profile-name old-prefix` (Wine names the profile `mn`).

- [ ] **Step 3: Implement** the run-game.sh lines (comment as the file's others: why `steamuser`, why the XDG folders).

- [ ] **Step 4: Run the gates**

Run: `sh eac-vm/tests/user-shares.sh && sh eac-vm/tests/modes.sh`
Expected: `user-shares: ok`, `modes: ok`. A failing `links-unchanged` or `real-documents-kept` disproves spec §8's Wine check: stop, back to the maintainer.

- [ ] **Step 5: Commit**

```bash
git add eac-vm/guest/run-game.sh eac-vm/tests/user-shares.sh
git commit -m "eac-vm guest: Wine runs as steamuser with the Mac's folders, so the shared profile keeps its links"
```

### Task 5: The docs say where a VM game's saves are

**Files:**
- Modify: `README.md` (the section on Steam, one paragraph: a game in the VM uses its Mac prefix's profile, `~/Documents` and its own folder, so Steam Cloud works as on macOS)
- Modify: `docs/superpowers/specs/2026-10-23-macneutron-vm-shared-saves-design.md` (Status line: implemented; §8's checks with their results)

- [ ] **Step 1:** Write both; privacy scan: `/usr/bin/grep -n -E "[0-9]{17}|/Users/[a-z]" README.md docs/superpowers/specs/2026-10-23-macneutron-vm-shared-saves-design.md` prints nothing but `/Users/<mac user>` placeholders.
- [ ] **Step 2: Commit** `git commit -m "docs: a VM game's saves are its Mac profile's"`

### Task 6: End to end, full gates, bench

- [ ] **Step 1: Full gates**

Run: `swift test && sh eac-vm/tests/user-shares.sh && sh eac-vm/tests/vm-setup.sh && sh eac-vm/tests/games-xattr.sh && sh eac-vm/tests/modes.sh && sh eac-vm/tests/d3d-smoke.sh`
Expected: all pass.

- [ ] **Step 2: The maintainer's run:** `make app APP=build/MacNeutron-next.app` (signing vars set); the maintainer opens it once, starts Palworld from Steam: its cloud saves show in the game; they save and quit.
- [ ] **Step 3: Check Steam's exit pass:** in `~/Library/Application Support/Steam/logs/cloud_log.txt`, Palworld's last `Starting sync (up,AC Exit,)` uploads the changed files (`Persisting file`/upload lines, no `Skipping un-modified` for them), and nothing is deleted. Print only with SteamIDs and account IDs masked.
- [ ] **Step 4: Bench once:** `eac-vm/tests/bench-smite2.sh` (the maintainer starts Steam); launch time and fps against Task 2's baseline. More than 10% worse goes to the maintainer before release.
