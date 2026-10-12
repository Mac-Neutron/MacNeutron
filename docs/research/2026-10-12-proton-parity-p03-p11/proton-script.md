# Proton 11 `proton` script: S: drive, library match, run argv (behavioural spec)

Source: `ValveSoftware/Proton`, ref `proton_11.0` (exists; head `bc896217ffcd`, 2026-10-09), file `proton`
(2189 lines), read with `gh api` as text and saved beside this file as `proton_11.0.proton.txt`. Every `proton:N`
below is a line in that file. `steam.c:N` is `steam_helper/steam.c` at the same ref (`steam_11.0.c.txt`).
README rows are `README.md` at the same ref. Commit ids are from `gh api repos/ValveSoftware/Proton/commits/<id>`.

## 1. `get_validated_steamapps_parent(orig_path)` — proton:299-309

Steps, in order (all inside one `try`, proton:300-306):

1. `path = os.path.realpath(orig_path)` (proton:301). All symlinks resolved, made absolute, trailing slash gone.
   `realpath("")` is the current directory.
2. `os.path.isdir(path)` (proton:302).
3. `os.path.basename(path) == "steamapps"` (proton:302). Exact, case-sensitive, and tested on the *resolved* path:
   a symlink named `steamapps` that points at `SteamLibrary` fails; `SteamApps` fails.
4. `parent = os.path.dirname(path)` (proton:303).
5. `os.access(parent, os.W_OK)` (proton:304): access(2), write permission on the parent for the real uid.
6. `os.stat(path).st_dev == os.stat(parent).st_dev` (proton:304): `steamapps` is not a mount point / not on another
   filesystem than its parent.
7. All pass: return `parent` (resolved, absolute) (proton:305).

There is no other check. Any exception is swallowed by a bare `except: pass` (proton:306).

Failure (any check false, or an exception): `log("Error: unable to use parent for game drive, path " + orig_path)`
(proton:308), which writes `Proton: Error: unable to use parent for game drive, path <orig>` to stderr only
(`log`, proton:72-80; not the `PROTON_LOG` file), then **returns `orig_path` unchanged** (proton:309): the raw,
unresolved `STEAM_COMPAT_LIBRARY_PATHS` entry, i.e. the `steamapps` folder itself.

Commit: `d93f8a66` "proton: Use parent dir of steamapps for gamedrive." (CW-Bug-Id #27482, #27297), the only change
that added this function; the message gives no rationale for the writable and same-device checks.

## 2. `setup_dir_drive` and when `dosdevices/s:` is created, retargeted or removed

### 2a. The function — proton:330-345 (wrappers 347-351)

```
setup_game_dir_drive()  = setup_dir_drive("gamedrive",  "s:", try_get_game_library_dir())   # proton:347-348
setup_steam_dir_drive() = setup_dir_drive("steamdrive", "t:", try_get_steam_dir())          # proton:350-351
```

- `drive_path = g_compatdata.prefix_dir + "dosdevices/" + drive_name` (proton:331), i.e.
  `$STEAM_COMPAT_DATA_PATH/pfx/dosdevices/s:` (`prefix_dir = compatdata + "/" + "pfx/"`, proton:634-635, 645-646).
  The link name is lowercase `s:`. The function never creates `dosdevices/` (it comes from the default prefix copy).
- `dest_dir` is an argument, so `try_get_game_library_dir()` (and its stderr error line) runs **even when gamedrive
  is off** (proton:348).
- Option on (`"gamedrive" in g_session.compat_config`, proton:332):
  - `dest_dir` falsy (`None`, or `""`): remove `drive_path` if `lexists` (proton:333-335).
  - `dest_dir` set and `drive_path` lexists: `cur_tgt = os.readlink(drive_path)`; if `cur_tgt != dest_dir` (plain
    string compare, no normalisation), `os.remove` then `os.symlink(dest_dir, drive_path)` (proton:337-341). Equal:
    nothing.
  - `dest_dir` set, nothing there: `os.symlink(dest_dir, drive_path)` (proton:342-343).
- Option off: remove `drive_path` if `lexists` (proton:344-345).
- Link target is an absolute path: the resolved parent (validated case) or the raw library entry (fallback).
- Not handled: a non-symlink file or a directory at `s:` makes `readlink`/`remove` raise, and nothing catches it,
  so the script aborts.

### 2b. When it runs

`main` (proton:2137-2186): `Session()` (2149) → `init_wine()` (2151) → `init_session(sys.argv[1] != "runinprefix")`
(2156) → verb dispatch (2160-2184). `init_session` calls `g_compatdata.setup_prefix()` only when that flag is true
(proton:2028-2029). `setup_prefix` calls `setup_game_dir_drive()` and `setup_steam_dir_drive()` at proton:1259-1260,
inside `with self.prefix_lock:` (proton:989), after `c:`/`z:` creation (1015-1019) and the Steam file copies.

| Verb | `s:` set by `setup_prefix` (1259, under `pfx.lock`) | again before running (2162, no lock) |
|---|---|---|
| `run` | yes | yes (proton:2160-2164) |
| `waitforexitandrun` | yes | no (proton:2165-2169) |
| `runinprefix` | **no** (`init_session(False)`); existing link left untouched | no |
| `destroyprefix` | yes (before `remove_tracked_files`, 2172-2173) | no |
| `getcompatpath` / `getnativepath` | yes, so `winepath` sees the current `S:` | no |
| unknown verb | yes, then "Need a verb." exit 1 (2182-2184) | no |

Both toolmanifests use `"commandline" "/proton %verb%"` (toolmanifest_x86_64.vdf:4, toolmanifest_arm64.vdf:4).
The second call for `run` came from `b01153b165` "proton: Also setup S: drive for game directory for run verb."
(CW-Bug-Id #19930; no further reason given). In 11.0 nothing between 1259 and 2162 changes its inputs
(`check_environment` is at 1772, before `setup_prefix` at 2029), so the second call is a no-op unless another
process retargeted the link in between.

## 3. Default, `PROTON_SET_GAME_DRIVE`, compat-config spelling, fallback

- **Default on, for every app:** `ret.add("gamedrive")` in `default_compat_config()` at function level, outside the
  `SteamAppId` block (proton:1456-1457, comment "options to also be enabled for prerequisite setup steps").
  `default_compat_config()` has **no per-app disable** in 11.0: the `ret.discard("gamedrive")` list (Natural
  Selection 2 4920, Talos VR 552440, Talos 257510) was removed by `fa493fa6`. Default-on came from `7b70313e`
  (author 2026-01-20, committed 2026-04-16), which deleted the per-app enable list.
- **Steam's `STEAM_COMPAT_CONFIG`** (proton:1533-1545): comma-separated tokens are only ever *added* to
  `compat_config` (1545; `cmdlineappend:` handled separately, 1538-1543). The spelling is `gamedrive`
  (README:332). There is **no** `nogamedrive` token: it would be added as an inert string that nothing reads
  (the only `no…` negation handled is `noforcelgadd`, proton:1547-1549). The only references to `"gamedrive"` are
  proton:332 (via 348), 1457, 1772.
- **`PROTON_SET_GAME_DRIVE`** (proton:1772) via `check_environment` (proton:1630-1637):
  - not in env → untouched (stays on);
  - `nonzero(v)` (`len(v) > 0 and v != "0"`, proton:57-58) → `compat_config.add("gamedrive")`;
  - `"0"` or `""` → `compat_config.discard("gamedrive")`. Any other value, including `"false"` or `"00"`, enables.
  - The env is `g_session.env` (copy of `os.environ`, proton:1513) plus `user_settings.py` keys that are not already
    set (proton:1728-1734, before 1772). So a launch option beats `user_settings.py`.
  - README:302-310: per-game use is `PROTON_SET_GAME_DRIVE=0 %command%`; "set the variable to `0`" disables.
  - Runs before `setup_prefix` (1772 < 2029), so it covers both calls.
- **Fallback when validation fails** (exact): `try_get_game_library_dir` returns
  `get_validated_steamapps_parent(entry)` (proton:320), which returns the raw `entry` (proton:309). `dest_dir` is
  that string, so `s:` → the `steamapps` folder itself (created or retargeted as in 2a), and the game sees
  `S:\common\<Game>\…`. This is the pre-`d93f8a66` layout and the original 2020 behaviour (`4c0f01e2dc` returned the
  entry directly). Spec Part A item 2 and Part C M1 are correct.
  - Edge: if the matched entry is `""`, the fallback returns `""`, which is falsy, so `s:` is **removed**
    (proton:333-335).
  - No library match, or either env var missing: `None`, so `s:` is removed (proton:312-314, 322).

## 4. Library pick: `try_get_game_library_dir` — proton:311-322

1. Both `STEAM_COMPAT_INSTALL_PATH` and `STEAM_COMPAT_LIBRARY_PATHS` must be keys of `g_session.env` (presence
   only; empty values pass), else `None` (proton:312-314).
2. `STEAM_COMPAT_LIBRARY_PATHS.split(":")` (proton:317). No escaping; empty pieces kept.
3. In list order, the **first** entry with `entry in STEAM_COMPAT_INSTALL_PATH` wins (proton:318-320). That is a
   Python **substring-anywhere** test: not a prefix test, not component-wise, no `realpath`, case-sensitive.
   Validation is applied only to that first match. If it fails, Proton falls back (section 3) and does **not** try
   later entries.
4. No match → `None` (proton:322).

Consequences of the substring test:
- An empty entry (`""` from `"a::b"`, a leading or trailing `:`, or an empty variable) matches every install path.
  The empty string reaches validation as `realpath("")` = the cwd.
- `/G/steamapps` matches `/Volumes/X/G/steamapps/common/Game` if it is listed first.
- A library-path spelling that differs from the install-path spelling (case, symlinked form, trailing slash) can miss,
  and then `s:` is removed.

On Linux, Steam's entries are `steamapps` folders. `setup_steam_files` strips the last component from each entry
(steam.c:795-796) to write `libraryfolders.vdf` (steam.c:786-815), which then lists `S:\` for the library behind
`s:`.

## 5. `run` verb argv, the three appids, and `steam.exe=b`

`Session.run` (proton:2094-2135):

- Adverb: `['steam-runtime-launcher-interface-0', 'proton']` if that program is on `PATH`, else nothing
  (proton:2095-2098).
- `PROTON_REMOTE_DEBUG_CMD` side process (2100-2107; parsed at 2023-2026).
- `SteamGameId` (string compare; not `SteamAppId`) in `311210` (CoD: Black Ops 3), `1985810` (Black Ops Cold War),
  `1549250` (Undecember) → `argv = [wine_bin, "c:\\Program Files (x86)\\Steam\\steam.exe"]` (proton:2109-2115). Plain
  `wine`, no preloader, `WINELOADERNOEXEC` not set.
- Otherwise:
  - x86_64 host (`host_pe_arch == "x86_64-windows"`): `WINELOADERNOEXEC=1` ("avoid restart through start.exe") and
    `[lib_dir/wine/x86_64-unix/wine-preloader, lib_dir/wine/x86_64-unix/wine, "c:\\windows\\system32\\steam.exe"]`
    (proton:2117-2120).
  - aarch64 host: `[wine_bin, "c:\\windows\\system32\\steam.exe"]` (proton:2121-2122; `bin-arm64` picked at 574-581).
- Final command: `adverb + argv + sys.argv[2:] + self.cmdlineappend` (proton:2124).
  - `sys.argv[2:]` is Steam's `%command%` tail: the game's absolute **Unix** path and its arguments. Python does not
    convert it.
  - `cmdlineappend` holds the `cmdlineappend:` items from `STEAM_COMPAT_CONFIG`, with `\,` escaping
    (proton:1538-1543, `comma_escaped` 1279-1285).
  - It runs with `env=self.env`, stdout and stderr to the log file (proton:2089-2092). Its rc becomes the exit code
    (2135, 2186).
- The conversion to `S:` happens in the helper: `run_process` converts the first argument if it starts with `/` or
  `"/`, using `wine_get_dos_file_name`, then requotes it (steam.c:443-521).
- Why the three appids (history; the CW bugs are private):
  - `0f1f692c70` (2021-10-04) moved the default to `c:\windows\system32\steam.exe`.
  - `b203ba6bdd` "Run steam from its Program Files (x86) location. For CoD: Black Ops 3." (CW #19362) also used
    `wine_bin` rather than `wine64_bin` for it.
  - `ae688238b9` "Force 32 bit steam.exe for Undecember." (CW #24110).
  - `2ec36931d4` added Cold War (CW #24174).
  - That location holds Valve's i386 `SteamService.exe`, copied as `steam.exe` (proton:1116-1124, only if
    `legacycompat/SteamService.exe` exists). With `steam.exe=b`, the builtin helper runs, but as a 32-bit process
    at the Steam client's path.
  - Inference, not in the commits: the process machine follows the on-disk image (spec cites
    W/dlls/ntdll/unix/process.c:262-292).
- `steam.exe=b`: set in `Session.__init__`'s `dlloverrides` (proton:1514-1515, "always use our special built-in
  steam.exe"). It is serialized and **appended** after any user `WINEDLLOVERRIDES` with `;` (proton:2066-2073).
- **Locoland exemption:** `if os.environ.get("SteamGameId", 0) == "352130": del self.dlloverrides["steam.exe"]`
  (proton:1523-1525). Reason: Locoland's own executable is named `steam.exe` (`33b0da5fb1`, CW #21737), and `=b`
  would replace it with the helper. Proton's argv for 352130 is unchanged (system32).

## 6. Other drive and path-conversion sites in `proton`

- `dosdevices/c:` → `../drive_c` and `dosdevices/z:` → `/`: created only if absent, never retargeted
  (proton:1015-1019).
- `t:` "steamdrive" (proton:324-328, 350-351, called at 1260 and 2163) → raw `STEAM_COMPAT_CLIENT_INSTALL_PATH`,
  with no validation. It is **opt-in** (`PROTON_SET_STEAM_DRIVE`, proton:1773, or the `steamdrive` token), and the
  link is removed otherwise.
- `unix_to_nt_file_name(p) = '\\??\\unix' + p` (proton:354-355), used for the OpenVR `config`/`log` paths
  (proton:389-392). The OpenVR runtime is set to `C:\vrclient\` (387).
- `getcompatpath` → `wine winepath -w <unix>`; `getnativepath` → `wine winepath <dos>` (proton:2174-2181). Both run
  after `setup_prefix`, so `S:` applies.
- `steam_in_pfx_dir = "drive_c/Program Files (x86)/Steam/"` (proton:643), the target of the legacycompat copies
  (1113-1124).
- No other site touches `s:`. All Unix→DOS conversion of the game command and of `libraryfolders.vdf` is in
  `steam.c` (443-521, 750-816, `escape_path_unix_to_dos` 127-140).

## Port notes for MacNeutron (P0-3 / P1-1)

- Corrections to the spec's line refs:
  - `setup_dir_drive` is proton:330-345, and the wrappers are 347-351 (the spec says 333-348).
  - `try_get_game_library_dir` is 311-322.
  - `check_environment` is 1630-1637 (the spec says 1629-1636).
  - The other cited lines (1259, 1457, 1515, 1523-1525, 1772, 2109-2115, 2117-2122, 2162) are exact.
- Part B P0-3 step 1 says "matching by path component". That deviates from proton:319 with no macOS reason. Port
  `contains` exactly (first match only, no retry), or record a D-line.
- Python `realpath` = Darwin `realpath(3)`. Swift `URL.resolvingSymlinksInPath()` strips a leading `/private`, so
  `/private/tmp/...` tests would produce a different link target string than Proton. Use `realpath(3)`.
- MacNeutron has no `STEAM_COMPAT_CONFIG` handling (no compat-config set, no `cmdlineappend`). The gamedrive
  switch therefore reduces to: on, unless the env has `PROTON_SET_GAME_DRIVE` equal to `"0"` or `""`.
- MacNeutron's `prepare` runs for run, waitforexitandrun and get*path, and not for runinprefix (Launcher.swift:83-121).
  That matches Proton's coverage. One call under the lock is enough; the 2162 repeat is a no-op.
- Three-appid path: on MacNeutron, `Steam\steam.exe` is our helper, not i386 `SteamService.exe`. Running it there
  reproduces the location only, not the 32-bit process, until WoW64 (P1-1 step 5).
