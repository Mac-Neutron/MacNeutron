# Proton 11 `steam_helper/steam.c`: behavioural spec for the MacNeutron port

Research only. Nothing in the repo was changed, nothing was built or run.

## Sources and citation key

- `steam.c:N` is ValveSoftware/Proton `steam_helper/steam.c` at ref **`proton_11.0`**. The ref exists, so no
  substitute branch was used. Text copy: `steam.c.txt` in this folder (1063 lines, BSD-3, Valve notice at 1-29).
- `PMk:N` is Proton `Makefile.in` at `proton_11.0` (`Makefile.in.txt`). `SHMk` is `steam_helper/Makefile.in` at the
  same ref.
- `proton.py:N` is Proton's `proton` script at `proton_11.0` (`proton.py.txt`).
- `VW:` is ValveSoftware/wine `proton_11.0`: `vw-winstation.c.txt` (win32u), `vw-server-winstation.c.txt`,
  `vw-process.c.txt` (ntdll/unix), `vw-wine.inf.in.txt`.
- `W/` is our Wine tree, `build/wine-arm64-src/wine/` (Wine 11.19 plus patches 0001-0036), as in the spec.
- `bridge/steam.c:N` and `Makefile:N` are this repo at `main` (77e5799).
- Commit messages: `gh api repos/ValveSoftware/Proton/commits/<sha>`. The full `steam.c` history at `proton_11.0` has 18
  commits since the C rewrite (`steam.c-log.txt`).

## Corrections and clarifications to the spec

**Corrections** (the spec's text or citations are wrong):
- **P1-1's restart wait "1013-1040".** The waiting side is steam.c:1003-1049. The restart event is created at
  1016-1026, and the restart loop runs 1028-1047.
- **L6's citation "1013-1023".** "Child is still running, not restarting" is at steam.c:1037-1043.
- **New, not in the spec: the restart sender can't signal on our Wine.** It opens the event with `SYNCHRONIZE` only
  (steam.c:688) and calls `SetEvent`, which our Wine refuses (§4). P1-1 step 3 must open with `EVENT_MODIFY_STATE`
  too. This was checked against our Wine and VW's server path, not against VW's in-process sync backends, so it says
  nothing about Proton on Linux.

**Clarifications** (the spec is right in scope; these add detail):
- **`--` handling.** P1-1 step 3 lists stripping a leading `--` as our change. For D-12 (spec line 483), note that
  Proton itself strips nothing. It forwards `--` verbatim to the native client (steam.c:717-719), and looks at `--`
  only for the restart test (673-683). D-12 should name the strip and the `-applaunch` mapping as our additions.
- **`wine_get_dos_file_name`.** P0-3 step 3's "via `GetProcAddress(kernel32)`" is the recipe for our mingw build.
  Proton calls it directly (steam.c:133, 394, 503), because its helper is a Wine builtin linked against Wine's kernel32
  import library (SHMk, PMk:462-470). Only `__wine_unix_spawnvp` goes through `GetProcAddress` there (700-709).
- **Helper architectures.** Spec line 443's "i386 builtin helper" is right for runs of `Steam\steam.exe`. In addition,
  PMk:467-470 builds steam.exe for i386, x86_64 and aarch64, and proton.py:2116-2122 runs the 64-bit one from
  `c:\windows\system32`. The i386 one runs only for `Steam\steam.exe` (`steam.exe=b`, proton.py:1515): the
  three-appid path (proton.py:2109-2115) and the `HKCR\steam` handler. Proton ships an aarch64 build of this file, so
  the code is ARM64-clean.

---

## 1. `run_process` (steam.c:417-649)

### 1a. Skipping argv[0] (419-441)
- `cmdline = GetCommandLineW()`, the full line including argv[0] (419).
- If it starts with `"`: `find_quote(cmdline + 1)` finds the first `"` preceded by an even number of backslashes
  (247-265). `cmdline` becomes the character after that quote (427-431).
- Otherwise it is the first `' '` (434). Only a space counts, not a tab.
- If `cmdline` is NULL: `ERR "Invalid command"` and return `INVALID_HANDLE_VALUE` (436-440). This returns **before**
  `*should_await` is written (585). `main`'s `should_await` (984) is uninitialised, so this path is undefined behaviour.
  It is reachable only when argv[0] is unquoted and a tab separates the arguments.
- It then skips spaces only (441).

### 1b. The Unix-path branch (443-521)
- **Trigger:** `cmdline[0] == '/'`, or `cmdline[0] == '"' && cmdline[1] == '/'` (444-445). Only the first argument is
  ever converted.
- **Quoted target (459-469):**
  - `start = cmdline + 1` and `end = find_quote(start)`. On no match: `ERR "Unmatched quote?"` and `goto run` with the
    line unchanged (463-467).
  - `remainder = end + 1`, which starts with the space before the next argument, if there is one.
  - The contents between the quotes are not unescaped: `\"` stays as typed.
- **Unquoted target (470-477):**
  - `end` is the first `' '`, or the terminating NUL. `remainder = end`.
  - A Unix path with a space in it would be split here. Wine's own command-line builder always quotes an argument that
    contains a space or tab, and always quotes argv[0] (W/dlls/ntdll/unix/env.c:1476-1534). Proton gets that line
    because it runs `wine c:\windows\system32\steam.exe <Steam's argv...>` (proton.py:2116-2124).
- **Narrowing (479-501):**
  - The target is copied into `scratchW` (481-483).
  - It is converted with `WideCharToMultiByte(CP_UNIXCP, ...)`, a size pass and then a convert pass (485-501). On
    failure: ERR and `goto run` with the line unchanged.
  - `scratchW` and `scratchA` are never freed. That leak is harmless.
- **Conversion (503):** `dos = wine_get_dos_file_name(scratchA)`.
  - The result is HeapAlloc'd and is never freed on this path.
  - **There is no NULL check:** 507 and 514 dereference `dos`. In our Wine it returns NULL only when not even the first
    path component can be opened (W/dlls/kernel32/path.c:491-509, the `i <= 9` break at 505).
  - How the drive is chosen is in §5.
- **Console detection (505-512):**
  - `CoInitialize(NULL)` is never balanced.
  - Then `console = SHGetFileInfoW(dos, 0, &sfi, sizeof sfi, SHGFI_EXETYPE)`. If it is non-zero and
    `HIWORD(console) == 0`, the code adds `CREATE_NEW_CONSOLE`.
  - Return values, from Wine's `shgfi_get_exe_type` (W/dlls/shell32/shell32_main.c:53-110, 196-200):

    | Target | Return value | Gets `CREATE_NEW_CONSOLE`? |
    |---|---|---|
    | DLL, unreadable, not an image | 0 | No |
    | DOS or PIF | `0x4d5a` | Yes |
    | Console PE | `IMAGE_NT_SIGNATURE` (HIWORD 0) | Yes |
    | GUI PE | `IMAGE_NT_SIGNATURE \| MajorSubsystemVersion<<24 \| Minor<<16` | No |
    | NE | `NE \| ver<<16` | No |

  - It relies on `GetBinaryTypeW` (W/dlls/kernel32/module.c:164-238). For an image our runtime can't map,
    `NtCreateSection` returns `STATUS_INVALID_IMAGE_WIN_64/32`, which still returns TRUE (208-213). The PE header is
    then read directly, so x64 and i386 console programs should still be detected on arm64. This is not run-verified.
- **New command line (514-520):** `"` + dos + `"` + remainder. The target is always re-quoted, and the arguments are
  kept byte for byte.

### 1c. The common part (523-589)
- `SetConsoleCtrlHandler(console_ctrl_handler, TRUE)` (526). The handler returns TRUE (267-270), so steam.exe ignores
  Ctrl-C and close events.
- `use_shell_execute = should_use_shell_execute(cmdline)` (528). It runs on the converted line. See §2.
- The EA block runs if ShellExecute is chosen and the line starts with `link2ea://` or `steam2ea://` (529-578). See §2.
- `hide_window = env_nonzero("PROTON_HIDE_PROCESS_WINDOW")` (579). `env_nonzero` means set, non-empty and first
  character not `'0'` (59-63).
- `*should_await = !use_shell_execute` (585).

### 1d. CreateProcess branch (629-648)
- `new_cmdline = fixup_cmdline_paths(cmdline)` (631, §3).
- If `hide_window`: `STARTF_USESHOWWINDOW` with `SW_HIDE` (633-637).
- `CreateProcessW(NULL, new_cmdline, NULL, NULL, FALSE, flags, NULL, NULL, &si, &pi)` (639).
  - Handles are **not** inherited.
  - `flags` is `CREATE_UNICODE_ENVIRONMENT` plus `CREATE_NEW_CONSOLE` from 1b. Nothing else: no `CREATE_SUSPENDED`.
  - The environment and current directory are inherited.
- On failure: ERR and return `INVALID_HANDLE_VALUE`. `new_cmdline` leaks (640-643).
- On success: free `new_cmdline` if fixup allocated it, close `pi.hThread`, and return `pi.hProcess` (644-647).

### Port notes
- **Win32 APIs:** GetCommandLineW, WideCharToMultiByte, wine_get_dos_file_name, CoInitialize, SHGetFileInfoW,
  SetConsoleCtrlHandler, CreateProcessW, CloseHandle.
- **Port these against our Wine.** It is the same Wine API family: `CP_UNIXCP` is `65010` (W/include/winnls.h:369),
  and on macOS Wine's Unix code page is UTF-8. Note the inconsistency: `escape_path_unix_to_dos` uses `CP_UTF8`
  (steam.c:137) where `run_process` uses `CP_UNIXCP`. Keep each as Proton has it.
- **Today's differences in our helper** (bridge/steam.c:197-244):
  - it uses `bInheritHandles=TRUE` and `CREATE_SUSPENDED`, then registers after the create;
  - its argv[0] skip treats tabs as separators;
  - it has no Unix-path branch and no console flag.

  The faithful port uses `FALSE` and no suspend. The suspend exists so that a failed start doesn't touch the
  registration. Proton writes its registration first, so moving to Proton's order is the parity choice.
- **Recommended hardening, documented as non-behavioural:**
  - `if (!dos) goto run;`
  - initialise `should_await = TRUE` in main, so "Invalid command" exits 1 deterministically.
- **macOS:** every default Steam path contains `Application Support`, so the quoted sub-branch (459-469) is always the
  one taken. The unquoted one is still needed for paths without spaces.

---

## 2. `should_use_shell_execute` and the ShellExecuteW branch (steam.c:282-362, 528-628)

### The decision (282-321)
- `get_end_of_excutable_name` (282-297) walks to the first space outside quotes. It toggles `quoted` on every `"`, with
  no escape handling.
- If the character before the end is `"`, step back one (305-306). Then step back 4. If that is still inside the line
  and the 4 characters equal `.exe` case-insensitively (`towlower`, 272-280), use CreateProcess. Anything else uses
  ShellExecute.
- So only argument 0 ending in `.exe` uses CreateProcess. `.bat`, `.cmd`, `.msi`, `.lnk`, URLs (`steam://`,
  `link2ea://`), documents and names shorter than 4 characters all go to ShellExecute.

### The EA workarounds (529-578), run before the launch
- **Condition:** `use_shell_execute && wcslen(cmdline) > 10`, and a case-sensitive `memcmp` of the raw line against
  `L"link2ea://"` (10 chars) or `L"steam2ea://"` (11 chars) (529-530). A quoted `"link2ea://..."` does not match.
  `steam2ea://` comes from 5b89db94 (CW #27639).
- **Sets** `link2ea = TRUE` (541).
- **Desktop close timeout:** `SetUserObjectInformationA(GetThreadDesktop(GetCurrentThreadId()), 1000, &3000, 4)`
  (532, 536, 542-543); failure is only logged.
  - Index 1000 is Valve-Wine-only: "set desktop close timeout" in milliseconds (VW win32u/winstation.c:765-804;
    VW server/winstation.c:905).
  - Valve's default `close_timeout_val` is 0 (VW server/winstation.c:300, 456-457). So the 3 s keeps explorer's
    desktop alive while EA's launcher hands off and relaunches.
  - **Our Wine** rejects index 1000 with `ERROR_INVALID_PARAMETER` (W/dlls/win32u/winstation.c:782-786). It has a fixed
    1 s close timeout (W/server/winstation.c:425-426). On our Wine this step only logs an ERR.
- **IsUnavailable:** `HKLM\Software\Electronic Arts\EA Desktop`, opened with `KEY_ALL_ACCESS` in the helper's own
  view. If `IsUnavailable` is a non-zero `REG_DWORD`, it is set to 0 (545-557).
- **The background service:** OpenSCManagerW(NULL, `SERVICES_ACTIVE_DATABASEW`, `SERVICE_QUERY_STATUS`), then
  OpenServiceW(`EABackgroundService`), then QueryServiceStatus. If the state is `STOP_PENDING` or `STOPPED`, it deletes
  `C:\ProgramData\EA Desktop\backgroundservice.ini` (558-577). This is part of 99a319f7, "more cases of broken EA
  installation" (CW #26492).

### The ShellExecute call (590-628)
- **Splitting the line:** it cuts at the end of argument 0 by writing a NUL in place, so `cmdline` is mutated.
  `param` is the rest, from the character after that space; it can start with more spaces (593-602). The file keeps
  its quotes. How Wine's ShellExecuteW treats a quoted `lpFile` was not verified in W.
- **The call:** `ShellExecuteW(NULL, L"open", cmdline, param, NULL, hide_window ? SW_HIDE : SW_SHOWNORMAL)`, looped
  while the result is below 32 (604). Each failure is logged.
- **`game_process && ret == SE_ERR_FNF (2) && link2ea` (607-612):**
  - `try_recover_eadesktop_symlink()`:
    1. It acts only if `C:\Program Files\Electronic Arts\EA Desktop\EA Desktop` fails to open with exactly
       `ERROR_FILE_NOT_FOUND`. A directory that exists, or any other error, returns FALSE (329-336).
    2. It enumerates the subdirectories of `...\EA Desktop\*.*` (338-344).
    3. For the first one where `<sub>\EA Desktop\Link2EA.exe` opens (345-349), it calls
       `CreateSymbolicLinkA("...\EA Desktop\EA Desktop", "<sub>\EA Desktop", DIRECTORY | ALLOW_UNPRIVILEGED_CREATE)`.
       The target is relative (350-352). Success returns TRUE.
  - FALSE breaks the loop. A second failure also breaks, because of `if (retry_count++) break`. So there is **at most
    one retry**.
- **`game_process && ret == SE_ERR_NOASSOC (31) && link2ea` (613-624):**
  - `MsiConfigureProductExW(L"{C2622085-ABD2-49E5-8AB9-D3D6A642C091}", 0, INSTALLSTATE_DEFAULT, L"REMOVE=ALL")`
    uninstalls EA Desktop.
  - Then `RegDeleteTreeW` of `HKLM\Software\Classes\link2ea`, `HKLM\Software\Electronic Arts\EA Desktop` and
    `HKLM\Software\Electronic Arts\EA Core`.
  - Then break, with no retry.
- **Any other error:** break (625).
- **Always returns `INVALID_HANDLE_VALUE`** (627), even on success. `should_await` is FALSE, so main does not wait on a
  child. For a game process it still waits on the wineserver shutdown event (§10).
- **Waits in this branch:** none apart from the single retry. The 3 s desktop timeout is the only timed element.

### Port notes
- **APIs:** GetThreadDesktop, SetUserObjectInformationA, RegOpenKeyExW, RegQueryValueExW, RegSetValueExW,
  OpenSCManagerW, OpenServiceW, QueryServiceStatus, DeleteFileW, ShellExecuteW, CreateFileA, FindFirstFileA,
  FindNextFileA, CreateSymbolicLinkA, MsiConfigureProductExW, RegDeleteTreeW.
- **Libraries:** shell32, advapi32, msi, user32.
- **Win32 coverage in our Wine:**
  - `CreateSymbolicLinkW` is implemented with reparse points (W/dlls/kernelbase/file.c:1010).
  - `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE` is in llvm-mingw's `winbase.h:3422`.
- **Registry view:** Proton's system32 helper is 64-bit, and so is ours (arm64), so the HKLM EA keys are read from the
  same view.
- **Without these, the branch is dead code on our runtime:**
  - EA Desktop is an x64 app;
  - the desktop-timeout index needs a VW Wine patch;
  - `SE_ERR_NOASSOC` recovery needs msi.

  Port them anyway: they are plain Win32, and the rule is parity.
- **`.bat` or `.cmd` targets** go through `ShellExecuteW`. How Wine runs them (through `cmd`) was not verified in W. The P1-1 check.sh row (".bat
  writes its marker through ShellExecute") covers it.
- **`steam://` as argument 0 of the game command line** (not as `argv[1]` of a fresh steam.exe) reaches ShellExecute
  here. That is a different path from the command handler.

---

## 3. `fixup_cmdline_paths`, the Source SDK `-game` path (steam.c:364-415, called at 631)

- **Gate:** the line contains the case-sensitive substring `\Source SDK Base` (371), **and** starts with `"` (373). It
  skips to the next `"` with no escape handling; with no closing quote, it returns the line unchanged (374-377).
- **Scanning:**
  - It skips whitespace (`iswspace`).
  - If the token is exactly `-game` followed by whitespace (381), it skips that and the whitespace that follows.
  - If the next character is not `/`, it `continue`s (385). The outer loop re-skips whitespace and treats the token as
    an ordinary one. A quoted `"-game "/path""` value is therefore never converted.
- **Conversion:**
  - The Unix path is the run of non-whitespace characters (388-389). It is narrowed by truncating each WCHAR to a
    char (390-393), so non-ASCII breaks.
  - It is converted with `wine_get_dos_file_name` (394). On NULL: trace, move on and continue (396-401).
- **Splicing:**
  - The DOS path goes into a new `malloc`ed buffer, **unquoted** (403-408).
  - It frees the DOS path with HeapFree (409) and returns the new buffer (410). **Only the first `-game` match is
    rewritten.**
- **No match:** it returns the original pointer (414). The caller frees only when the pointer differs (644).
- **Provenance:** a8b51747, "Convert Unix game path in cmd line for Source SDK 2007" (CW #27044).

### Port notes
- **APIs:** wcsstr, wcsncmp and iswspace (CRT), and wine_get_dos_file_name.
- **macOS:**
  - Mac Steam's library path contains a space (`Application Support`). A `-game ~/Library/Application Support/...`
    token would be cut at the space, and only `~/Library/Application` converted.
  - The converted path `S:\steamapps\sourcemods\...` or `Z:\...` is inserted unquoted. Once S: is in place (P0-3), it
    usually has no spaces.
  - Port it faithfully; the space limit is then shared with Proton. Widening it to quoted tokens would be a documented
    deviation with a macOS reason (the default path has a space).

---

## 4. `steam_command_handler` (steam.c:652-748) and the restart

### Which arguments count as a command (669-671)
- If `argc > 1` and `argv[1]` is neither prefixed by `steam://` nor starts with `-`, return FALSE: it is not a command,
  so main goes on to the game flow.
  - "Prefixed" means `StrStrIA(argv[1], "steam://") == argv[1]`, a case-insensitive prefix test.
- So these all are commands: `steam://...` in any letter case, `-applaunch ...`, `--`, `-silent`, and so on.
- **`argc == 1` (no arguments) is also handled:** it is not rejected, and it falls through to forwarding with no
  arguments. `main` returns 0 (928-929).

### The restart, sender side (673-698)
- **Condition:** `argc > 2`, `argv[1]` exactly `"--"`, `SteamGameId` set, and `argv[2]` string-equal (case-sensitive
  `strcmp`) to `steam://launch/<sgi>` or `steam://rungameid/<sgi>` (snprintf into a 64-byte buffer).
  - No trailing slash, no extra arguments, no other letter case.
  - Without the leading `--`, `steam.exe steam://rungameid/<id>` is **forwarded**, not a restart.
- **Action:**
  - `OpenEventW(SYNCHRONIZE, FALSE, L"PROTON_STEAM_EXE_RESTART_APP")`, then `SetEvent` and `CloseHandle` (686-693).
    **The handle has only `SYNCHRONIZE`, and `SetEvent`'s result is ignored.** Our Wine requires
    `EVENT_MODIFY_STATE` on both paths:
    - in-process (msync): `get_inproc_sync` returns `STATUS_ACCESS_DENIED` (W/dlls/ntdll/unix/sync.c:789-793, called
      from `inproc_set_event` at 868);
    - server: `event_op` calls `get_event_obj(..., EVENT_MODIFY_STATE)` (W/server/event.c:357), and VW has the same
      check (VW server/event.c:417).

    So on our Wine a faithful port's `SetEvent` fails with access denied. It still logs "Signalled app restart" and
    exits 0, and the game never restarts. Proton fixed the same bug for the DRM ack event in db9e6ffb ("Open ack event
    with EVENT_MODIFY_STATE", CW #27580) but not here. **The port must open with
    `SYNCHRONIZE | EVENT_MODIFY_STATE`**, a correctness deviation with Valve's own precedent; record it as a D-line. A
    faithful port would fail the P1-1 restart test (L6) for this reason, independent of the child-exit timing.
  - If the event is missing: `ERR "Restart event not found."` (696).
  - It returns TRUE in both cases (697), so the sender exits 0 and the URL is **not** forwarded.
- **How it is reached:** `HKCR\steam\shell\open\command` = `"%16426%\Steam\Steam.exe" -- "%1"`
  (VW loader/wine.inf.in:316; `%16426%` is Program Files (x86)). So a game's `ShellExecute("steam://rungameid/<id>")`
  produces `argv = [Steam.exe, --, steam://rungameid/<id>]`.

### Forwarding to the native client (700-747)
- **Resolving the call:** `__wine_unix_spawnvp` is fetched once with
  `GetProcAddress(GetModuleHandleW(L"ntdll"), "__wine_unix_spawnvp")` and kept in a static (700-709). If it is
  missing, return FALSE, and main goes on to the game flow with these arguments.
- **The argv:** `unix_argv` is `argv[1..argc-1]` copied verbatim, including `--` and any `-` options, NULL-terminated
  (711-719).
- **The two tries:**
  - `unix_argv[0]` is `steam-runtime-steam-remote`, then `steam` (662-667, 721-733).
  - Each runs `p__wine_unix_spawnvp(unix_argv, TRUE)`, stopping at the first `STATUS_SUCCESS`.
- **What "success" means:**
  - `wait=TRUE` forks, `execvp`s and `waitpid`s. It returns the child's **exit status** as the NTSTATUS, or the errno
    status if the exec fails (W/dlls/ntdll/unix/process.c:470-516; PE-side thunk W/dlls/ntdll/loader.c:3535-3540;
    export W/dlls/ntdll/ntdll.spec:1770).
  - So "forwarded" means the native tool exited 0. The helper **blocks** until it does.
- **Result:** on failure it logs the argv and the status, then **returns TRUE anyway** (736-747), so steam.exe exits 0.
- **Encoding:** `argv` is the CRT's ANSI argv, in the Windows ACP, not UTF-8. Non-ASCII in a URL reaches the Unix side
  as ACP bytes. Proton has the same issue.

### The restart, waiting side in main (1003-1049)
- **Waits[0]:** for a game process it is **not the child**. It is the wineserver shutdown event, because
  `NtSetInformationProcess(ProcessWineMakeProcessSystem)` at 1004-1005 overwrites `wait_handle` (W/dlls/ntdll/unix/
  process.c:1843-1852). For a non-game process it is the child, and only when CreateProcess was used.
- **The restart event:** `CreateEventW(NULL, FALSE /*auto-reset*/, FALSE, L"PROTON_STEAM_EXE_RESTART_APP")`, only for a
  game process (1016-1026). It is created **after** the first `run_process` (989), so a sender that runs the moment
  the game starts can miss it ("Restart event not found").
- **`FreeConsole()`** (1027).
- **The loop (1028):** `while ((ret = WaitForMultipleObjects(wait_count, waits, FALSE, INFINITE) != WAIT_OBJECT_0))`.
  This is a **precedence bug**: `ret` gets the comparison, 0 or 1, not the wait result.
  - `WAIT_OBJECT_0` ends the loop.
  - **Every** other result, `WAIT_OBJECT_0+1` but also `WAIT_FAILED` and `WAIT_ABANDONED`, sets `ret = 1`. The
    `"Wait failed"` branch (1032-1036) is unreachable.
  - With `wait_count == 1` (the event couldn't be created, or this isn't a game process), a failed wait would be taken
    as a restart.
- **Restart body (1037-1046):**
  - If the child exists and `WaitForSingleObject(child, 0) == WAIT_TIMEOUT`: `ERR "Child is still running, not
    restarting."` and `continue`. The auto-reset signal is **consumed and lost**.
  - Otherwise it closes the child and calls `run_process` again on the **same** `GetCommandLineW()`. The `should_await`
    from that call is ignored, and `waits[0]` is not updated; for a game process it is the shutdown event, so that
    doesn't matter.
- **Teardown:** `CloseHandle(waits[1])` (1048).
- **Timing:** the shutdown event is set only after the last non-system process has exited **and** `master_socket_timeout`
  has passed (W/server/process.c:513-528, 550-556; 3 s by default, W/server/main.c:48). A restart signal that arrives
  within about 3 s of the game's exit still restarts it. A **persistent wineserver** (`-p` with no value, so
  `TIMEOUT_INFINITE`) never signals it, and steam.exe would never exit.

### Port notes
- **APIs:** StrStrIA (shlwapi), OpenEventW, SetEvent, GetModuleHandleW, GetProcAddress, CreateEventW,
  WaitForMultipleObjects, WaitForSingleObject, FreeConsole, and `__wine_unix_spawnvp` (Wine export).
- **Linux-only:** the `steam-runtime-steam-remote` and `steam` binaries. On macOS both `execvp`s fail with ENOENT, so
  the forward is logged and steam.exe still exits 0. That is our behaviour today for any `steam://` that reaches the
  helper.
- **D-12 replacement:**
  - `__wine_unix_spawnvp({"/usr/bin/open", <url>, NULL}, TRUE)`. `open` exits 0 once Launch Services accepts the URL.
  - Stripping `--` and mapping `-applaunch <id>` to `steam://rungameid/<id>` are our additions, to be documented
    under D-12.
  - Decide what `steam.exe` with no arguments should do on Mac:
    - faithful: forward nothing, log, exit 0;
    - or `open steam://open/main`.
- **Precedence bug:** recommended to port the **intended** semantics. Use `ret = WaitForMultipleObjects(...)`, break
  on anything but `WAIT_OBJECT_0+1`, and note it in a comment. This differs from Proton only on a wait failure, which
  never occurs with valid handles. A strictly faithful port copies the expression; that is the maintainer's call.
- **L6 test design:**
  1. The test helper exits **before** the second `steam.exe -- steam://rungameid/$SteamGameId` signals. The event is
     auto-reset, and the check is a 0 ms `WaitForSingleObject(child, 0)`.
  2. The signal must arrive within the server's 3 s window.
  3. The second steam.exe is itself a user process, which keeps the window open while it runs.
  4. Check our launcher doesn't start wineserver with `-p` (infinite), or steam.exe never returns.

---

## 5. `setup_steam_files`: `libraryfolders.vdf` and the S: conversion (steam.c:113-182, 750-816)

### Steps
1. Create `C:\Program Files (x86)\Steam\config` and `C:\Program Files (x86)\Steam\steamapps`. Return on any failure
   except `ERROR_ALREADY_EXISTS` (759-769). Nothing is written into `config`.
2. Buffer `"LibraryFolders"\n{\n` (771), grown with `strappend` (150-182).
3. **Entry 0**, `STEAM_COMPAT_CLIENT_INSTALL_PATH`, if set (774-784):
   - `escape_path_unix_to_dos` (127-148) does `wine_get_dos_file_name`, doubles every `\`, converts with
     `WideCharToMultiByte(CP_UTF8)` and NUL-terminates. Proton has fixed bugs here: 655f6565 (missing NUL) and
     8c0fbeb0 (garbage written).
   - The entry is `\t"%u"\n\t{\n\t\t"path"\t\t"%s"\n\t}\n` with `index++`. The index fix is 09827855.
4. **One entry per `:`-separated item in `STEAM_COMPAT_LIBRARY_PATHS`** (786-812):
   - `next` is the `:` or the end of the string (792).
   - `end = next`. If `end[-1] == '/'`, step back once. Then walk back to just after the previous `/` (795-796).
     `end` is now one past the parent's trailing slash.
   - `lstrcpynA(path, start, end - start)` copies `end - start - 1` characters, **dropping the trailing slash**, then
     NUL-terminates (798-799).
   - So `/a/b/steamapps` and `/a/b/steamapps/` both give `/a/b`, the **parent of the library folder**.
   - The parent is converted the same way. A failure logs and skips the entry, without a gap in the index.
   - Edge cases: an item without `/` gives `end == start`, and `lstrcpynA(n=0)` writes nothing, so `path` is
     uninitialised. `/steamapps` gives `""`, the conversion fails, and the entry is skipped.
5. Close with `}\n` and `write_file(C:\Program Files (x86)\Steam\steamapps\libraryfolders.vdf)`, using
   `CREATE_ALWAYS`, so it is rewritten on every game start (113-125, 814-815).

### How Wine picks S:
- `wine_get_dos_file_name` builds `\??\unix\<path>` and NtOpenFiles it, walking up a component at a time until a
  directory opens. It then calls `GetFinalPathNameByHandleW(VOLUME_NAME_DOS)` and re-appends the part that doesn't
  exist (W/dlls/kernel32/path.c:470-525).
- Opening `\??\unix\...` rewrites the NT name through `find_drive_nt_root` (W/dlls/ntdll/unix/file.c:4378-4394).
- `find_drive_nt_root` walks from the **deepest** component up to the root. For each directory, it takes the first
  drive letter, in A-Z order, whose root has the same dev and inode (W/dlls/ntdll/unix/file.c:4229-4281).
- So with `dosdevices/s:` pointing at the library's parent, the library parent converts to `S:\` and the game to
  `S:\steamapps\common\...`.
- If another letter maps the same folder, the lower letter wins.
- With no drive at all, the result is `\\?\unix\...`. Z: (`/`) normally prevents that.

### macOS specifics (ties to D-8 and M1)
- Mac Steam's `STEAM_COMPAT_LIBRARY_PATHS` is `~/Library/Application Support/Steam/steamapps`, so its parent **is**
  `STEAM_COMPAT_CLIENT_INSTALL_PATH`. Entries 0 and 1 would name the same folder:
  - both `S:\` when S: points there;
  - both `Z:\Users\...\Steam` without S:.
- Leaving out entry 0 (D-8) only drops a duplicate when the paths are equal. On Linux they differ for secondary
  libraries.
- `:` separators are safe on macOS, because Steam paths don't contain `:`.

### Port notes
- **APIs:** CreateDirectoryW, CreateFileW, WriteFile, CloseHandle, lstrcpynA, WideCharToMultiByte, HeapFree,
  GetProcessHeap, and wine_get_dos_file_name.
- **Keep the off-by-one semantics.** A port using `memcpy`/`strndup(start, end - start)` would keep the trailing `/`;
  Wine strips it anyway, so the output is the same, but say so.
- **Ours today:** `PrefixManager.writeSteamFiles` and `SteamBridge.libraryFolders` write `Z:` (spec P0-3). This moves
  into the helper, gated on `SteamGameId` (§9).

---

## 6. The dummy `vguiPopupWindow` "Steam" window and `GetDesktopWindow` (steam.c:92-111, 941-944)

- **The thread** `create_steam_windows` (92-111):
  - It registers class `vguiPopupWindow` with `DefWindowProcW` and `hInstance` 0.
  - It creates `CreateWindowW("vguiPopupWindow", "Steam", WS_POPUP, 40, 40, 400, 300, NULL, ...)`, which is never
    shown (no `WS_VISIBLE`).
  - It creates `CreateWindowA("static", "SteamVR Status", WS_POPUP, 0, 0, 0, 0, ...)` (102). This one is 5c4a5301,
    "Create dummy SteamVR status window" (CW #24552).
  - Then it pumps messages until `WM_QUIT`, which never comes.
  - The consumer is a game's `FindWindow("vguiPopupWindow", "Steam")` or `FindWindow(NULL, "SteamVR Status")`.
- **Main thread:**
  - `GetDesktopWindow()` (942), then `CreateThread(create_steam_windows)` (944), then the environment writes (949-958).
  - Why, from 42f903ef (Esme Povirk, CW #26589): win32u's `get_desktop_window` reads the process environment without
    locking. When it runs first on the background thread, it races the main thread's `SetEnvironmentVariableW`, and
    explorer.exe sometimes receives a corrupt environment. Creating the desktop on the main thread first avoids that.
  - **Our Wine has the same code:** `get_desktop_window` (W/dlls/win32u/winstation.c:805) passes
    `peb->ProcessParameters->Environment` and its size straight to `NtCreateUserProcess` for explorer (862-863). Keep
    Proton's order: GetDesktopWindow, then CreateThread, then SetEnvironmentVariableW.
- **Related:** `ProcessWineMakeProcessSystem` releases the desktops of steam.exe's existing threads
  (W/server/process.c:1786-1790), so the dummy windows don't keep the desktop alive.

### Port notes
- **APIs:** RegisterClassExW, CreateWindowW, CreateWindowA, GetMessageW, TranslateMessage, DispatchMessageW,
  GetDesktopWindow, CreateThread. Library: user32, which `Makefile:30` doesn't link today.
- **No macOS-specific issue:** the windows are hidden `WS_POPUP`.
- **Test** (spec P1-1): `FindWindowW(L"vguiPopupWindow", L"Steam")` from the game succeeds. There is a short race
  after start, because the thread creates the window asynchronously; the test should poll.

---

## 7. The GamingRepair mark (steam.c:72-90, called at 947)

- `RegOpenKeyExW(HKLM, "SOFTWARE\Valve\Steam\Apps", 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY)`, which is
  `HKLM\Software\Wow6432Node\Valve\Steam\Apps` for a 64-bit helper. If it fails, return.
- `RegOpenKeyExA(appkey, sgi)`. The **open** is never a create. If it succeeds, set `GamingRepair` to `REG_DWORD 1`.
- Rationale (comment 72-74; 1f6e6a80, CW #26142): Xbox Game Studios titles run GamingRepair.exe until this flag is
  set. It uses the unimplemented AppInstallManager, so it never succeeds and costs about 20 s.

### Port notes
- **APIs:** RegOpenKeyExW, RegOpenKeyExA, RegSetValueExW, RegCloseKey (advapi32).
- **L5 holds:** the key exists only after Steam's install scripts create it, and they don't run on our runtime. The
  port is a no-op until P1-4. It is 10 lines, so porting it now does no harm if the maintainer prefers the order.
- **Related:** our bridge already writes the same 32-bit view's parent key (bridge/steam.c:108-112, matching
  VW wine.inf.in:1408).

---

## 8. `PROTON_WAIT_ATTACH` (steam.c:904-914, 963-977)

- If `env_nonzero("PROTON_WAIT_ATTACH")`, loop `Sleep(100)` until `IsDebuggerPresent() || is_ptraced()`. A trace is
  printed every 10 iterations (about 1 s). There is no timeout.
- **Position:** inside the `SteamGameId` block, after `setup_steam_files` and before the game is started, so a
  debugger can attach to steam.exe first.
- `is_ptraced` calls `NtQueryInformationProcess(GetCurrentProcess(), 1100 /* ProcessWineUnixDebuggerPid */)` and is
  FALSE on any error.
  - Class 1100 is a **Valve Wine extension** (VW include/winternl.h:2037; VW dlls/ntdll/unix/process.c:1628-1632).
  - Only Linux implements it, by reading `TracerPid` from `/proc/self/status` (VW process.c:1067-1081). Every other
    platform returns 0, even in Valve's Wine. That includes macOS: the `HAVE_MACH_MACH_H` branch (VW
    process.c:1006, 1027-1030), plus 1108-1111 and 1120-1123.
  - **Our Wine has no class 1100** (no match in W/include/winternl.h or W/dlls/ntdll/unix/process.c). So
    `is_ptraced()` is always FALSE here, and only a Windows-side debugger (winedbg) ends the wait.

### Port notes
- **APIs:** IsDebuggerPresent, Sleep, NtQueryInformationProcess (ntdll).
- **macOS:** a native lldb attach to the Wine process isn't detected. That would need a Wine-side
  `sysctl(KERN_PROC_PID)` `P_TRACED` check. Attaching also needs `get-task-allow` on wine.app, which is out of scope.
- Port it faithfully, including the 1100 query, which is harmless. Document that only winedbg ends the wait on Mac.

---

## 9. Gating on `SteamGameId` (steam.c:931-980, also 986, 999, 1003, 1016)

- `sgi = getenv("SteamGameId")`. Set means non-NULL; **an empty string counts as set** (931).
- **Only for a game process:**
  - both named events;
  - the desktop and window thread;
  - ActiveProcess `pid`;
  - GamingRepair;
  - `SteamPath` and `ValvePlatformMutex`;
  - `setup_steam_registry`, which loads `lsteamclient` and calls `steamclient_init_registry` (206-221);
  - `setup_steam_files`;
  - the `PROTON_WAIT_ATTACH` loop;
  - `setup_vr_registry`, which loads `vrclient_x64` under `_WIN64` and calls `vrclient_init_registry` (184-204,
    986-987);
  - the DRM thread (999-1000);
  - `ProcessWineMakeProcessSystem` (1003-1005);
  - the restart event (1016-1026).
- `game_process` is also passed to `run_process`, where it enables the EA recovery (607, 613).
- **Without `SteamGameId`:** a plain launcher. It runs the target, and waits only on a CreateProcess child.

### Port notes
- **Today's helper** writes the registry whether or not `SteamGameId` is set (bridge/steam.c:245), and has neither the
  restart nor the window.
- **Registry Proton writes itself:** only `ActiveProcess\pid` (65-70). `SteamClientDll`, `SteamClientDll64`,
  `SteamPath`, `SteamExe` and the Wow6432Node `InstallPath` come from VW wine.inf.in:1394-1408, which has a `PID`
  placeholder of `0xfffe`; value names are case-insensitive, so `pid` overwrites it.
  - Our bridge writes these itself (bridge/steam.c:71-125), because our prefix isn't built from Valve's wine.inf. They
    stay as prefix setup, but under the gate.
  - `ActiveUser` (bridge/steam.c:104-105) has no Proton counterpart (L2).
- **vrclient:** `vrclient_x64` isn't shipped on our runtime, so `LoadLibraryW` fails, logs an ERR and continues
  (195-196). Port it as is; it is harmless.
- **lsteamclient:** Proton calls `LoadLibraryW(L"lsteamclient")`. On our runtime it is in the prefix under that name
  and as `steamclient64.dll` (spec P0-1). Wire it with P1-2's "write keys first, then init" rule (Part A, M4).

---

## 10. `main()`: the exact order and exit codes (steam.c:916-1063)

1. **Command handler** (928-929): if it returns TRUE, **exit 0**. That covers `steam://...`, any `-...` argument, no
   arguments at all, and a restart signal, whether or not the event was found.
2. **If `SteamGameId` is set** (931-980), in this order:
   1. `CreateEventW(NULL, FALSE, FALSE, "Steam3Master_SharedMemLock")` (936).
   2. `CreateEventW(..., "Global\\Valve_SteamIPC_Class")`, for the 2K Launcher (939).
   3. `GetDesktopWindow()` (942).
   4. `CreateThread(create_steam_windows)` (944).
   5. `set_active_process_pid()`: `HKCU\Software\Valve\Steam\ActiveProcess`, `pid` = own pid, via `RegSetKeyValueW`
      (946, 65-70).
   6. `set_gamingrepair_succeeded(sgi)` (947).
   7. `SetEnvironmentVariableW("SteamPath", "C:\\Program Files (x86)\\Steam")` (949).
   8. `GetModuleFileNameW(NULL)`, with `\` turned into `/`, set as `ValvePlatformMutex` (950-958). Provenance:
      9480ccd2 (CW #25738).
      - The default launch gives `c:/windows/system32/steam.exe`. The three appids give
        `c:/Program Files (x86)/Steam/steam.exe` (proton.py:2109-2122).
      - The letter case Wine reports was not verified (L7).
   9. `setup_steam_registry()` (960).
   10. `setup_steam_files()` (961).
   11. The `PROTON_WAIT_ATTACH` loop (963-977).
   12. `game_process = TRUE` (979).
3. **If `argc > 1`** (982-1001):
   1. `setup_vr_registry()` if this is a game process.
   2. `child = run_process(&should_await, game_process)`.
   3. If `should_await` and `child == INVALID_HANDLE_VALUE`: **exit 1**. That is a CreateProcess failure, or the
      uninitialised "Invalid command" case.
   4. `wait_handle = child`, when awaiting.
   5. For a game process: `CreateThread(steam_drm_thread, child)`. The DIPC semaphores `STEAM_DIPC_CONSUME` and Valve's
      spelling `SREAM_DIPC_PRODUCE` are created **inside** that thread, after the child has started (863-902). Our
      helper creates them before resuming the child (bridge/steam.c:251-255), which is stricter, not less faithful.
4. **For a game process:** `NtSetInformationProcess(GetCurrentProcess(), 1000, &wait_handle, sizeof(HANDLE *))`
   (1003-1005).
   - steam.exe becomes a system process, and `wait_handle` becomes the server's shutdown event (W/server/process.c:
     1767-1794).
   - If the call fails, `wait_handle` keeps its value: the child, or `INVALID_HANDLE_VALUE` after ShellExecute.
5. **If `wait_handle` is valid:** the restart event, `FreeConsole`, and the wait/restart loop (1007-1049, §4).
6. **Cleanup:** close the two events (1051-1054). If `child` is valid, `GetExitCodeProcess(child, &rc)` and close it
   (1056-1060).
7. **`return rc`** (1062).

**Exit codes:**

| Code | When |
|---|---|
| 0 | The command handler handled the command. |
| 0 | The ShellExecute path, success or failure: `child` is invalid, so `rc` stays 0. |
| 0 | `argc == 1` and the command handler returned FALSE (spawnvp missing). |
| 1 | `should_await` and the first `run_process` returned `INVALID_HANDLE_VALUE`. |
| Child's exit code | Otherwise, from the **last** child after any restarts. `STILL_ACTIVE` (259) is possible if the shutdown event fired while that child was still alive. |

A `GetExitCodeProcess` failure leaves 0.

### Port notes
- **APIs:** CreateEventW, GetDesktopWindow, CreateThread, RegSetKeyValueW, SetEnvironmentVariableW,
  GetModuleFileNameW, LoadLibraryW, GetProcAddress, FreeLibrary, NtSetInformationProcess, NtOpenDirectoryObject,
  NtQueryDirectoryObject, CreateSemaphoreW, ReleaseSemaphore, GetProcessId, GetExitCodeProcess.
- **Wine-only info class:** 1000, `ProcessWineMakeProcessSystem`, is present in ours (W/include/winternl.h:2014;
  W/dlls/ntdll/unix/process.c:1843-1852) and already used (bridge/steam.c:261).
- **`main(int argc, char **argv)`:** Proton's command handler needs the CRT's ANSI argv. Today our helper uses
  `main(void)` plus `GetCommandLineW`.
- **Teardown:** Proton never clears `pid` at exit. Our `unregister_steam` (bridge/steam.c:129-138) is an addition, and
  needs a D-line or removal under the parity rule.
- **DRM ack directory:** Proton's `find_ack_event` opens `\BaseNamedObjects\Session\1` (824); ours opens
  `\Sessions\1\BaseNamedObjects` (bridge/steam.c:164). Not in this task's scope; flagged for whoever ports `main`.

---

## Build: what the arm64 llvm-mingw port needs (whole file)

- **Proton's build:** a Wine builtin, `MODULE = steam.exe`, `IMPORTS = user32 advapi32 shlwapi shell32 msi ole32`,
  `EXTRADLLFLAGS = -mwindows` (GUI subsystem, SHMk), built against msvcrt (500c38f3), for i386, x86_64 and aarch64
  (PMk:462-470).
- **Ours:** `Makefile:30` builds with `aarch64-w64-mingw32-clang -O2 -static -s $(PE_MARCH) ... -ladvapi32 -lntdll`,
  console subsystem.
- **Add:** `-luser32 -lshell32 -lshlwapi -lole32 -lmsi`. All five exist in
  `build/dxmt-src/llvm-mingw/aarch64-w64-mingw32/lib/`.
- **Header and import gaps under llvm-mingw:**
  - `wine_get_dos_file_name` isn't in mingw's `libkernel32.a`. Use
    `typedef WCHAR *(CDECL *pwgdfn)(const char *)` via `GetProcAddress(GetModuleHandleW(L"kernel32"),
    "wine_get_dos_file_name")` (W/dlls/kernel32/kernel32.spec:1740, cdecl). Free the result with
    `HeapFree(GetProcessHeap(), 0, p)`, because it comes from `RtlAllocateHeap` (W/dlls/kernel32/path.c:482, 490,
    511).
  - `CP_UNIXCP`: `#define CP_UNIXCP 65010` (W/include/winnls.h:369); it isn't in mingw's headers.
  - `__wine_unix_spawnvp`: already dynamic in Proton (700-709). It works the same from mingw (W/dlls/ntdll/
    ntdll.spec:1770).
  - `wine/debug.h` (`WINE_TRACE`, `WINE_ERR`, `TRACE`, `ERR`, `WARN`, `debugstr_*`): replace with `fprintf(stderr,
    ...)` as the bridge does today, or with no-ops for TRACE.
  - `RTL_CONSTANT_STRING`, `ARRAY_SIZE`, `DIRECTORY_BASIC_INFORMATION`, `NtOpenDirectoryObject` and
    `NtQueryDirectoryObject`: define or declare them locally. The bridge already declares the last three
    (bridge/steam.c:140-143).
  - `ProcessWineMakeProcessSystem` (1000) and `ProcessWineUnixDebuggerPid` (1100): cast literals, as the bridge does.
  - `SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE`, `SHGFI_EXETYPE`, `MsiConfigureProductExW`, `RegSetKeyValueW` and
    `RegDeleteTreeW` are all present in llvm-mingw's headers. `_WIN32_WINNT` defaults to `0x601`, which is enough for
    the Reg* calls.
- **ARM64:** nothing in steam.c is architecture-specific apart from the `_WIN64` vrclient name. Proton already builds
  it for aarch64.
- **Subsystem (open):** Proton is `-mwindows` and calls `FreeConsole` before waiting. Ours is console subsystem plus
  `FreeConsole` (bridge/steam.c:263-264). Whether to add `-mwindows` affects the console a console child inherits when
  `CREATE_NEW_CONSOLE` isn't set, and where `fprintf(stderr)` goes. Untested; decide with a check.sh row.
- **Licence:** steam.c is BSD-3. `bridge/Proton-steam-LICENSE.txt` and the header in bridge/steam.c:13-43 already carry
  the notice, so close porting is fine.
