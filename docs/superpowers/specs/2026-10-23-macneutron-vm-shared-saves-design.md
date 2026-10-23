# A game in the VM uses the saves Steam Cloud syncs

Status: approved 2026-10-23; implemented 2026-10-23 on branch vm-shared-saves (plan
`docs/superpowers/plans/2026-10-23-macneutron-vm-shared-saves.md`); §8's checks passed.
Parent: `2026-10-15-macneutron-eac-vm-route-design.md` (every game runs in the VM since 0.6.0).

## 1. Goal

Since 0.6.0 a game's prefix lives on its own VM disk (`/home/mn/pfx` on `state/<appid>.img`), while Steam Cloud syncs
the Mac's prefix, `steamapps/compatdata/<appid>/pfx`. Nothing connects the two. A game in the VM sees none of its cloud
saves, and nothing played in the VM reaches Steam Cloud: it lives only on the VM disk. Found 2026-10-23 with Palworld:
Steam downloaded its 162 cloud files into the Mac's prefix at launch, and the game in the VM started without them.
Steam's exit pass found the Mac's files unchanged and uploaded or deleted nothing, so the cloud copy is intact.

Done when:
- Palworld in the VM shows the saves Steam Cloud has.
- A save made in the VM is on the Mac when the game quits, and Steam's exit pass uploads it (`cloud_log.txt`).
- A game that writes beside its own .exe can do so in the VM.
- The gates in §7 pass.

## 2. Decisions (maintainer, 2026-10-23)

| Question | Decision |
|---|---|
| How the VM and the Mac share saves | The Mac prefix's user profile is shared into the VM and becomes the VM prefix's profile (approach A). Not a copy in and back at exit (a crash loses the session), not the whole prefix on the Mac (it collides with macOS Wine's own). |
| What a VM game sees as Documents | The real `~/Documents`, as macOS Wine already gives a game. |
| The game's folder | Writable in the VM, that game's folder only; the rest of the library stays read-only. |
| Saves already on VM disks | Not migrated: the Mac's copy is the one that counts. The VM was only used for testing, so the old VM profiles (`users/mn`) stay on their disks, unused. |

## 3. What the VM sees on each run

| In the VM | From the Mac | Access |
|---|---|---|
| `/mnt/games` | the game's library, `steamapps/common` | read-only (as today) |
| `/mnt/games/<game>` | `steamapps/common/<game>` | read-write (new) |
| `C:\users\steamuser` = `/home/mn/pfx/drive_c/users/steamuser` | `compatdata/<appid>/pfx/drive_c/users/steamuser`, resolved: its link's target when macOS Wine made it a link to its own profile (Palworld: `users/<mac user>`) | read-write (new) |
| `/Users/<mac user>/Documents` | `~/Documents` | read-write (new) |
| `/Users/<mac user>/{Desktop,Downloads,Music,Pictures,Movies}` | nothing: links to folders on the VM disk (`/home/mn/<name>`) | so the profile's links name a folder that's there |

Everything else in the prefix (`drive_c/windows`, `Program Files`, the registry, `ProgramData`) stays on the game's
VM disk: Wine's own files never go over virtiofs and never meet macOS Wine's prefix.

The Mac profile's links stay as macOS Wine made them: `Desktop`, `Documents`, `Downloads`, `Music`, `Pictures` and
`Videos` (→ `~/Movies`) are absolute links to the Mac's folders, which the VM now has at the same paths; the XP-name
links (`Application Data` → `AppData/Roaming`, `My Documents` → `Documents`, `Local Settings/Application Data` →
`../AppData/Local`, ProfileLinks.swift) are relative and work unchanged.

A game with no Mac profile yet (never run on macOS, nothing in the cloud) gets `pfx/drive_c/users/steamuser` made by the
launcher as a real folder: Steam's and Proton's layout. If the game later runs on macOS, PrefixManager's
`steamsOwnFolder` and ProfileLinks already merge such a folder into macOS Wine's profile.

## 4. Components

### 4.1 libkrun: a share without ownership (patch 0012)

libkrun's macOS virtiofs (`src/devices/src/virtio/fs/macos/passthrough.rs` at our pin a4d501ef) defaults to
`PermissionSemantics::LinuxComplete`: a Mac file without libkrun's `user.containers.override_stat` xattr shows its
real owner (uid 501), so the guest's `mn` (uid 1000) can't write a Mac user's 755 folders; and the guest's chown and
chmod write that xattr onto the Mac's files. `LinuxSimplified` reports every file as owned by the guest process asking
and keeps permission bits as the Mac file's own mode, with no xattrs. The C API (`krun_fs_device_new`,
`_new_read_only`) can't select it.

The permission mode is fixed when the device is made (`Fs::new` takes it), so patch 0012 adds a constructor:
`FsDevice::new_simplified(tag, host_path)` in `src/libkrun/src/api/device_builders.rs`, exported by ffier as
`krun_fs_device_new_simplified`, which makes the device with `LinuxSimplified`. Such a share serves no extended
attributes (as `LinuxSimplified`'s own documentation says; patch 0011's `xattr` switch is off for it), and the macOS
passthrough no longer stores a create's security context (`security.selinux`, which the guest kernel sends with
create, mkdir, mknod and symlink) on a share without them: the first gate run found it on every file the guest made. Our fork only (no upstream PR without the maintainer).

### 4.2 macneutron-vm

A repeatable `--user-share <tag>=<host dir>`: a writable virtiofs device in simplified mode for each. The existing
`share` and `games` devices are unchanged (`games` keeps its read-only device and patch 0011).

### 4.3 The launcher (MicroVM.swift, Launcher.runInMicroVM)

Before the boot, for the game's appid:
- the profile: `compatdata/<appid>/pfx/drive_c/users/steamuser`, resolved through its link if it is one; made as a
  real folder (and its parents) if missing;
- Documents: `~/Documents`, which must be listable: when macOS's privacy settings refuse it to Steam (or it's
  missing) the launch is refused before the boot, naming System Settings > Privacy & Security > Files and Folders;
- the game folder: the target's `steamapps/common/<game>` (from `MicroVM.guestPath`).

It removes a `mounts.failed` an earlier run left, passes `--user-share profile=… --user-share documents=… --user-share
game=…` and writes the run share's `mounts` file, one `<tag> <guest path>` line each:

```
profile /home/mn/pfx/drive_c/users/steamuser
documents /Users/<mac user>/Documents
game /mnt/games/<game>
```

plus `MN_MAC_HOME=/Users/<mac user>` in the guest script's header (run-game.sh's XDG folders, §4.5). The home is the
Mac user's real home path, never `~`; the documents line's mount point is always `$MN_MAC_HOME/Documents`.

### 4.4 mn-runner (root, in the guest)

Before it hands over to `mn`, when the share has a `mounts` file:
- The guest's root is read-only EROFS, so `/Users` is a tmpfs. Beside the documents line's mount point it makes
  `Desktop`, `Downloads`, `Music`, `Pictures` and `Movies` as links to `/home/mn/<name>` (folders on the state disk,
  owned by `mn`), so what a game puts there survives the run.
- For each line it makes the mount point and runs `mount -t virtiofs <tag> <path>`. A guest path outside
  `/home/mn/pfx/drive_c/users/`, `/Users/` and `/mnt/games/`, or one with a `..` component, is refused.
- On any failure it writes the reason to the share's `mounts.failed`, and to `run.log`, and powers off without running
  `run.sh` (no `run.status`): a game never runs on a profile that exists only in the VM. A marker, not a status:
  `run.status` carries the game's own exit status, which can be any number.

The game mount goes over the game's folder inside the read-only `games` mount (a mount point on a read-only virtiofs
mount needs only the directory, which exists).

### 4.5 run-game.sh

Wine runs with `USER=steamuser`, so its profile is `users/steamuser` like Proton's: Wine 11.20 names the profile from
`$USER` (`set_home_dir`, `dlls/ntdll/unix/loader.c`). `$HOME/.config/user-dirs.dirs` names the Mac's six folders under
`$MN_MAC_HOME` (`XDG_VIDEOS_DIR` = `…/Movies`), so when Wine makes the profile's folder links (a new prefix, or a
prefix update after a Wine change) it makes exactly the links macOS Wine made. Nothing else changes; `wineserver -w`
and the rest stay as they are.

### 4.6 The launcher's refusal

A run that left `mounts.failed` ends with a notification, "MacNeutron couldn't share this game's saves with the game VM:
<mounts.failed's first line>", and the reason in `launcher.log`. Steam then runs its exit pass on the untouched Mac profile.

## 5. Safety

- Steam Cloud: the profile is always the folder Steam watches, so its exit pass sees the real files. Nothing renames or
  relinks the prefix during a run (the `up,AC Exit` hazard: watched files missing then are deleted from the cloud).
- One copy of a game at a time: Steam starts one per app, macOS or VM, so no new lock.
- What a VM game reaches on the Mac: `~/Documents` (as on macOS), its profile, and its own game folder. The rest of the
  library stays read-only.
- Ownership: the Mac's files keep their owner and gain no xattrs; files the VM makes are the Mac user's.

## 6. Edge cases

- A library on another drive: the game share follows the game's own library, as `/mnt/games` does.
- `users/steamuser` as a link (macOS Wine made the profile) or a real folder (Steam's cloud download made it): the VM
  gets the folder it resolves to either way.
- APFS is case-insensitive: Wine handles both (macOS Wine already runs on it).
- Speed: AppData now goes over virtiofs. One bench, run once: SMITE 2's lobby for 2 minutes, launch time and fps
  against today's.

## 7. Testing

- Swift unit tests: the launcher passes the three shares and writes `mounts`; the profile resolves as a link, as a real
  folder, and when missing (made); a run that left `mounts.failed` is refused with its reason.
- Guest gate `eac-vm/tests/user-shares.sh` (`profile.sh` is the guest profiler's): a file written in the VM's profile, Documents and game folder is on the Mac,
  owned by the Mac user, with no xattrs, and a guest chmod is the file's own mode; the Mac profile's links are byte-identical after `wineboot -u` in the VM; a
  failed mount stops the run before Wine starts.
- End to end (the maintainer): Palworld in the VM shows its cloud saves; save, quit; `cloud_log.txt` shows the upload.

## 8. Checks before relying on the design

These were believed from the sources; all passed in `eac-vm/tests/user-shares.sh` on 2026-10-23 (rows in brackets):
- Wine's profile-link code (shell32 `_SHCreateSymbolicLinks` at wine-11.20) leaves an existing real `Documents` folder
  alone and recreates a link only to the XDG folder named, so a Steam-made real folder keeps its files [real-documents-kept, links-unchanged].
- libkrun's simplified mode handles symlinks (`readlink`, `symlink`) and `rename` across the profile as LinuxComplete
  does [link-rename].
- A virtiofs mount over a directory of the read-only `games` mount works in the guest kernel [odd-name].
- `wineboot` makes a new prefix around a `drive_c/users/steamuser` that is already a mount (mn-runner mounts before
  any Wine process runs) [profile-name].
- Wine with `USER=steamuser` on a prefix made as `users/mn` uses `users/steamuser` from that run on (the profile path
  comes from `$USER` at each start; the registry's shell folders use `%USERPROFILE%`) [old-prefix].

## 9. Not in this spec

- Steam Cloud's `ProgramData` root (few games use it).
- Migrating saves from old VM disks (§2).
- The VM's exit hang (a leftover process keeps `wineserver -w` waiting; investigated separately).

## 10. Prerequisite

`build/` was deleted on 2026-10-23, so nothing here builds or runs until the full rebuild: libkrun, the host tools,
the guest images and the builder. It is the plan's first step.
