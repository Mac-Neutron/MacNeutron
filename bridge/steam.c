/* steam.exe for MacNeutron: tells Windows games inside Wine that Steam is running.
 *
 *   steam.exe <program> [arguments...]      (<program>: a Windows path, or a macOS path, which it converts)
 *   steam.exe [--] steam://… | -applaunch <id> [arguments] | (nothing)   (Steam's commands: to Mac Steam, or a restart)
 *
 * steam_api(64).dll treats Steam as running when HKCU\Software\Valve\Steam\ActiveProcess\pid names a
 * live process, and loads the client DLL named next to it. This writes those values (pointing at the
 * runtime's Steam bridge, which the launcher copies into C:\Program Files (x86)\Steam) with the rest of
 * what Proton's steam.exe sets up for a game (register_steam, main), runs <program>, answers Steam's DRM
 * start check (see answer_drm), waits until no program is left in the prefix (launchers start the real
 * game and quit), and exits with <program>'s exit code. A GUI program, as Proton's (-mwindows).
 * Spec: docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md, Amendment 2 (Proton 11 parity).
 *
 * It follows Valve's Proton, steam_helper/steam.c, in much of its behaviour, and find_ack_event and answer_drm
 * closely, under Proton's notice (also in bridge/Proton-steam-LICENSE.txt):
 *
 * Copyright (c) 2015, 2019, 2020, 2021, 2022 Valve Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <windows.h>
#include <winternl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define STEAM_DIR L"C:\\Program Files (x86)\\Steam"
#define STEAM_KEY L"Software\\Valve\\Steam"
#define ACTIVE_PROCESS_KEY STEAM_KEY L"\\ActiveProcess"
#define CP_UNIXCP 65010  /* Wine's Unix code page (Wine's winnls.h); not in mingw's headers */

/* Proton's WINE_ERR (its TRACEs are left out): Wine's debug output, which goes to Wine's Unix stderr, the game log, even
 * when steam.exe has no Windows standard handles, as when a game's steam:// link starts it through HKCR\steam (Wine's
 * shell32 gives it a new console). Not gated by WINEDEBUG. Formatted wide and written as UTF-8: a letter above U+00FF
 * doesn't cut the line. */
static void err_line(const WCHAR *format, ...)
{
    static int (CDECL *output)(const char *);
    WCHAR wide[1024];
    char line[ARRAYSIZE(wide) * 3];
    va_list ap;
    int n;

    va_start(ap, format);
    _vsnwprintf(wide, ARRAYSIZE(wide) - 1, format, ap);  /* unterminated when cut or an exact fit */
    va_end(ap);
    wide[ARRAYSIZE(wide) - 1] = 0;
    if (!(n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, line, sizeof(line), NULL, NULL))) return;
    /* Wine's debug line holds 1020 bytes and raises past them (ntdll's append_output): a longer one is cut, at a
     * character boundary, and still ends the line. */
    if (--n > 1000) for (n = 1000; (line[n] & 0xc0) == 0x80; --n);
    if (!n || line[n - 1] != '\n') line[n++] = '\n';
    line[n] = 0;
    if (!output) output = (void *)GetProcAddress(GetModuleHandleW(L"ntdll"), "__wine_dbg_output");
    if (output) output(line);
    else fputs(line, stderr);
}
/* Wide formats: %ls for a WCHAR string, %hs for a char one. */
#define ERR(format, ...) err_line(L"steam.exe: " format, ##__VA_ARGS__)

/* Wine's kernel32 export (kernel32.spec), which mingw's import library lacks: a Unix path as a DOS one, on its deepest
 * drive (S: for a game in a Steam library). Freed with HeapFree. */
static WCHAR *wine_get_dos_file_name(const char *path)
{
    static WCHAR *(CDECL *convert)(const char *);

    if (!convert) convert = (void *)GetProcAddress(GetModuleHandleW(L"kernel32"), "wine_get_dos_file_name");
    return convert ? convert(path) : NULL;
}

/* An environment variable in Wine's Unix code page (UTF-8), as wine_get_dos_file_name takes paths; malloc'd, or NULL.
 * getenv gives the Windows code page instead, which mangles a non-ASCII path ("Lib Ünï"): Wine's lookup then stalls on
 * it (seen in bridge/check.sh). Proton's setup_steam_files uses getenv; this is hardening over it. */
static char *getenv_unix(const WCHAR *name)
{
    WCHAR *wide;
    char *value = NULL;
    DWORD len;
    int size;

    if (!(len = GetEnvironmentVariableW(name, NULL, 0)) || !(wide = malloc(len * sizeof(WCHAR)))) return NULL;
    if (GetEnvironmentVariableW(name, wide, len) < len
        && (size = WideCharToMultiByte(CP_UNIXCP, 0, wide, -1, NULL, 0, NULL, NULL))
        && (value = malloc(size)))
        WideCharToMultiByte(CP_UNIXCP, 0, wide, -1, value, size, NULL, NULL);
    free(wide);
    return value;
}

/* Proton's env_nonzero: set, and its first character isn't 0. */
static BOOL env_nonzero(const char *name)
{
    const char *value = getenv(name);
    return value && *value && *value != '0';
}

static HKEY create_key(HKEY root, const WCHAR *path)
{
    HKEY key;
    return RegCreateKeyExW(root, path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &key, NULL) ? NULL : key;
}

static void set_string(HKEY key, const WCHAR *name, const WCHAR *value)
{
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
}

static void set_dword(HKEY key, const WCHAR *name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
}

/* What a game process gets: ActiveProcess\pid, as Proton's set_active_process_pid, and the keys Valve's Wine writes
 * into every Proton prefix (wine.inf: SteamPath, SteamExe, SteamClientDll*, InstallPath), which ours isn't built
 * with. Not ActiveProcess\SteamPath, and SteamClientDll only with a 32-bit client (D-26). */
static void register_steam(DWORD pid)
{
    WCHAR apps[64];
    const WCHAR *value;
    DWORD appid;
    HKEY key;

    if ((key = create_key(HKEY_CURRENT_USER, STEAM_KEY)))
    {
        set_string(key, L"SteamPath", STEAM_DIR);
        set_string(key, L"SteamExe", STEAM_DIR L"\\steam.exe");
        /* Steam's UI language, which the launcher reads from macOS Steam's registry.vdf; some games read it here. */
        if ((value = _wgetenv(L"MACNEUTRON_STEAM_LANGUAGE")) && *value) set_string(key, L"language", value);
        RegCloseKey(key);
    }
    /* What the Windows client keeps for the game it runs. Under Proton lsteamclient's steamclient_init_registry writes
     * it, which needs a connection to Steam; these need none. The app ID is SteamAppId's, what steam_api's GetAppID
     * reports; 0 or none is no game. Running stays 1 after the game, as under Proton. */
    appid = (value = _wgetenv(L"SteamAppId")) ? wcstoul(value, NULL, 10) : 0;
    _snwprintf(apps, ARRAYSIZE(apps), STEAM_KEY L"\\Apps\\%lu", appid);
    if (appid && (key = create_key(HKEY_CURRENT_USER, apps)))
    {
        set_dword(key, L"Installed", 1);
        set_dword(key, L"Running", 1);
        set_dword(key, L"Updating", 0);
        RegCloseKey(key);
    }
    if ((key = create_key(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY)))
    {
        set_dword(key, L"pid", pid);
        set_string(key, L"SteamClientDll64", STEAM_DIR L"\\steamclient64.dll");
        if (GetFileAttributesW(STEAM_DIR L"\\steamclient.dll") != INVALID_FILE_ATTRIBUTES)
            set_string(key, L"SteamClientDll", STEAM_DIR L"\\steamclient.dll");
        /* An account ID earlier MacNeutron builds wrote; Proton writes none (parity L2). */
        RegDeleteValueW(key, L"ActiveUser");
        RegCloseKey(key);
    }
    if ((key = create_key(HKEY_LOCAL_MACHINE, L"Software\\Wow6432Node\\Valve\\Steam")))
    {
        set_string(key, L"InstallPath", STEAM_DIR);
        RegCloseKey(key);
    }
    /* Steam's URL handler: Proton's command (Valve's wine.inf), and an empty URL Protocol value, which wine.inf lacks
     * (R9, D-26). A game's ShellExecute("steam://…") runs the Steam folder's steam.exe, whose command handler restarts
     * the game or sends the URL on to Mac Steam. */
    if ((key = create_key(HKEY_CLASSES_ROOT, L"steam")))
    {
        set_string(key, L"URL Protocol", L"");
        RegCloseKey(key);
    }
    if ((key = create_key(HKEY_CLASSES_ROOT, L"steam\\shell\\open\\command")))
    {
        set_string(key, NULL, L"\"" STEAM_DIR L"\\steam.exe\" -- \"%1\"");
        RegCloseKey(key);
    }
}

/* ntdll's object-directory calls, which mingw's headers leave out. */
typedef struct { UNICODE_STRING ObjectName, ObjectTypeName; } DIRECTORY_BASIC_INFORMATION;
NTSTATUS NTAPI NtOpenDirectoryObject(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
NTSTATUS NTAPI NtQueryDirectoryObject(HANDLE, void *, ULONG, BOOLEAN, BOOLEAN, ULONG *, ULONG *);

/* Steam's start channel (DIPC), which Proton's steam.exe answers (steam_helper/steam.c, steam_drm_thread): a DRM-wrapped
 * game (CEG, the Steam client's start check) takes a slot from the SREAM_DIPC_PRODUCE semaphore (Valve's spelling),
 * creates an event named STEAM_START_ACK_EVENT<something>, signals STEAM_DIPC_CONSUME and waits for Steam to set the
 * event. SteamStub 3.x's "Application load error 3:0000065432" is another check: its signature check of
 * steamclient64.dll, which Wine patch 0036 and Valve's signed DLL answer (MACNEUTRON_STEAMCLIENT64). */
static HANDLE drm_consume, drm_produce;

/* The game's ack event: its name's suffix isn't known, so look through the session's named objects. */
static HANDLE find_ack_event(void)
{
    static const WCHAR prefix[] = L"STEAM_START_ACK_EVENT";
    union { DIRECTORY_BASIC_INFORMATION info; char buffer[1024]; } entry;
    UNICODE_STRING name;
    OBJECT_ATTRIBUTES attr;
    ULONG context = 0, size;
    BOOLEAN first = TRUE;
    HANDLE dir, ack = NULL;

    /* ponytail: Wine puts every process in session 1. */
    RtlInitUnicodeString(&name, L"\\Sessions\\1\\BaseNamedObjects");
    InitializeObjectAttributes(&attr, &name, 0, NULL, NULL);
    if (NtOpenDirectoryObject(&dir, 1 /* DIRECTORY_QUERY */, &attr)) return NULL;
    while (!ack && !NtQueryDirectoryObject(dir, &entry, sizeof(entry), TRUE, first, &context, &size))
    {
        first = FALSE;
        if (entry.info.ObjectName.Length >= sizeof(prefix) - sizeof(WCHAR)
            && !wcsncmp(entry.info.ObjectName.Buffer, prefix, ARRAYSIZE(prefix) - 1))
            ack = OpenEventW(EVENT_MODIFY_STATE, FALSE, entry.info.ObjectName.Buffer);
    }
    CloseHandle(dir);
    return ack;
}

static DWORD WINAPI answer_drm(void *arg)
{
    HANDLE ack;

    while (!WaitForSingleObject(drm_consume, INFINITE))
    {
        if ((ack = find_ack_event()))
        {
            SetEvent(ack);
            CloseHandle(ack);
        }
        /* In the game's log: a SteamStub error after "found" is about something else (its signature checks). */
        fprintf(stderr, "steam.exe: SteamStub start check: ack event %s\n", ack ? "found and set" : "not found");
        ReleaseSemaphore(drm_produce, 1, NULL);
    }
    return 0;
}

/* From here to run_process: Proton's steam_helper/steam.c (find_quote ... run_process), ported to mingw. */
static WCHAR *find_quote(WCHAR *str)
{
    WCHAR *end = wcschr(str, '"'), *ch;
    int odd;
    while (end)
    {
        odd = 0;
        ch = end - 1;
        while (ch >= str && *ch == '\\')
        {
            odd = !odd;
            --ch;
        }
        if (!odd)
            return end;
        end = wcschr(end + 1, '"');
    }
    return NULL;
}

static BOOL WINAPI console_ctrl_handler(DWORD type)
{
    return TRUE;
}

static BOOL streq_niw(const WCHAR *l, const WCHAR *r, size_t len)
{
    while (len > 0)
    {
        if (towlower(*l) != towlower(*r)) return FALSE;
        ++l; ++r; --len;
    }
    return TRUE;
}

static WCHAR *get_end_of_excutable_name(WCHAR *cmdline)
{
    BOOL quoted = FALSE;
    WCHAR *executable_name_end = cmdline;

    /* find the end of the first arg... */
    while (*executable_name_end != '\0' && (*executable_name_end != ' ' || quoted))
    {
        quoted ^= *executable_name_end == '"';
        executable_name_end++;
    }
    return executable_name_end;
}

/* ShellExecute anything that isn't a program: a URL, a batch file, a shortcut, a document. */
static BOOL should_use_shell_execute(WCHAR *cmdline)
{
    BOOL use_shell_execute = TRUE;
    const WCHAR *executable_name_end = get_end_of_excutable_name(cmdline);

    /* if the executable is quoted backtrack a bit */
    if (*(executable_name_end - 1) == '"') --executable_name_end;
    /* backtrack to before the end of the arg and check if we end in .exe or not */
    executable_name_end -= strlen(".exe");
    if (executable_name_end >= cmdline)
    {
        if (streq_niw(executable_name_end, L".exe", 4)) use_shell_execute = FALSE;
    }
    return use_shell_execute;
}

static BOOL try_recover_eadesktop_symlink(void)
{
    WIN32_FIND_DATAA ff;
    HANDLE handle, file;
    char path[MAX_PATH];

    handle = CreateFileA("C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop", GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0);
    if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_NOT_FOUND)
    {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        return FALSE;
    }
    if ((handle = FindFirstFileA("C:\\Program Files\\Electronic Arts\\EA Desktop\\*.*", &ff)) == INVALID_HANDLE_VALUE)
        return FALSE;
    do
    {
        if (!(ff.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (!strcmp(ff.cFileName, ".") || !strcmp(ff.cFileName, "..")) continue;
        sprintf(path, "C:\\Program Files\\Electronic Arts\\EA Desktop\\%s\\EA Desktop\\Link2EA.exe", ff.cFileName);
        file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, 0);
        if (file == INVALID_HANDLE_VALUE) continue;
        CloseHandle(file);
        sprintf(path, "%s\\EA Desktop", ff.cFileName);
        if (CreateSymbolicLinkA("C:\\Program Files\\Electronic Arts\\EA Desktop\\EA Desktop", path,
                                SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        {
            FindClose(handle);
            return TRUE;
        }
    } while (FindNextFileA(handle, &ff));
    FindClose(handle);
    return FALSE;
}

/* Source SDK Base's -game path, a Unix path, to DOS (only the first one, as Proton). Hardening over Proton, which
 * converts an unquoted path only, and byte by byte: a quoted one too (Wine quotes an argument with a space, as every
 * path under ~/Library/Application Support is), and as UTF-8, as run_process converts (D-line). */
static WCHAR *fixup_cmdline_paths(WCHAR *cmdline)
{
    const WCHAR argname[] = L"-game";
    WCHAR *p, *next, *dos_path, *ret, *wide;
    unsigned int len, path_len, extra;
    char *unix_path;
    BOOL quoted;
    int r;

    if (!wcsstr(cmdline, L"\\Source SDK Base")) return cmdline;
    p = cmdline;
    if (*p != '\"') return cmdline;
    ++p;
    while (*p && *p != '\"') ++p;
    if (!*p) return cmdline;
    ++p;
    while (*p)
    {
        while (iswspace(*p)) ++p;
        if (!wcsncmp(p, argname, ARRAYSIZE(argname) - 1) && iswspace(p[ARRAYSIZE(argname) - 1]))
        {
            p += ARRAYSIZE(argname) - 1;
            while (iswspace(*p)) ++p;
            if ((quoted = *p == '"' && p[1] == '/'))
            {
                next = ++p;  /* the path between the quotes, which stay */
                while (*next && *next != '"') ++next;
            }
            else if (*p == '/')
            {
                next = p + 1;
                while (*next && !iswspace(*next)) ++next;
            }
            else continue;
            wide = malloc((next - p + 1) * sizeof(*wide));
            memcpy(wide, p, (next - p) * sizeof(*wide));
            wide[next - p] = 0;
            dos_path = NULL;
            if ((r = WideCharToMultiByte(CP_UNIXCP, 0, wide, -1, NULL, 0, NULL, NULL)) && (unix_path = malloc(r)))
            {
                if (WideCharToMultiByte(CP_UNIXCP, 0, wide, -1, unix_path, r, NULL, NULL))
                    dos_path = wine_get_dos_file_name(unix_path);
                free(unix_path);
            }
            free(wide);
            if (!dos_path)
            {
                p = next;
                continue;
            }
            path_len = wcslen(dos_path);
            /* Wine keeps trailing separators, and Windows reads \" as a quote: each doubled before the closing one. */
            for (extra = 0; quoted && extra < path_len && dos_path[path_len - 1 - extra] == '\\'; ++extra);
            len = wcslen(cmdline) + 1;
            ret = malloc((len + path_len + extra - (next - p)) * sizeof(*ret));
            memcpy(ret, cmdline, (p - cmdline) * sizeof(*ret));
            memcpy(ret + (p - cmdline), dos_path, path_len * sizeof(*ret));
            for (r = 0; r < (int)extra; ++r) ret[(p - cmdline) + path_len + r] = '\\';
            memcpy(ret + (p - cmdline) + path_len + extra, next, (len - (next - cmdline)) * sizeof(*ret));
            HeapFree(GetProcessHeap(), 0, dos_path);
            return ret;
        }
        while (*p && !iswspace(*p)) ++p;
    }
    return cmdline;
}

/* Starts our command line minus argv[0]: a Unix path converted to DOS (S: for a game in a Steam library; a console
 * program gets its own console), anything not ending in .exe through ShellExecute. Returns the child, or
 * INVALID_HANDLE_VALUE (ShellExecute, or a failure); *should_await says whether to wait for it. */
static HANDLE run_process(BOOL *should_await, BOOL game_process)
{
    WCHAR *cmdline = GetCommandLineW();
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD flags = CREATE_UNICODE_ENVIRONMENT;
    BOOL use_shell_execute = TRUE, link2ea = FALSE;
    BOOL hide_window;

    /* skip argv[0] */
    if (*cmdline == '"')
    {
        cmdline = find_quote(cmdline + 1);
        if (cmdline) cmdline++;
    }
    else
        cmdline = wcschr(cmdline, ' ');
    if (!cmdline)
    {
        ERR("Invalid command\n");
        return INVALID_HANDLE_VALUE;
    }
    while (*cmdline == ' ') cmdline++;

    /* convert absolute unix path to dos */
    if (cmdline[0] == '/' || (cmdline[0] == '"' && cmdline[1] == '/'))
    {
        WCHAR *scratchW, *start, *end, *dos, *remainder, *new_cmdline;
        char *scratchA;
        size_t argv0_len;
        int r;
        DWORD_PTR console;
        SHFILEINFOW sfi;

        if (cmdline[0] == '"')
        {
            start = cmdline + 1;
            end = find_quote(start);
            if (!end)
            {
                ERR("Unmatched quote? %ls\n", cmdline);
                goto run;
            }
            remainder = end + 1;
        }
        else
        {
            start = cmdline;
            end = wcschr(start, ' ');
            if (!end) end = wcschr(start, '\0');
            remainder = end;
        }
        argv0_len = end - start;
        scratchW = malloc((argv0_len + 1) * sizeof(WCHAR));
        memcpy(scratchW, start, argv0_len * sizeof(WCHAR));
        scratchW[argv0_len] = '\0';
        r = WideCharToMultiByte(CP_UNIXCP, 0, scratchW, -1, NULL, 0, NULL, NULL);
        if (!r)
        {
            ERR("Char conversion size failed?\n");
            goto run;
        }
        scratchA = malloc(r);
        r = WideCharToMultiByte(CP_UNIXCP, 0, scratchW, -1, scratchA, r, NULL, NULL);
        if (!r)
        {
            ERR("Char conversion failed?\n");
            goto run;
        }
        /* Hardening over Proton, which goes on with NULL: an unconvertible path runs as given. */
        if (!(dos = wine_get_dos_file_name(scratchA)))
        {
            ERR("Can't convert %ls to a DOS path\n", scratchW);
            goto run;
        }
        CoInitialize(NULL);
        console = SHGetFileInfoW(dos, 0, &sfi, sizeof(sfi), SHGFI_EXETYPE);
        if (console && !HIWORD(console)) flags |= CREATE_NEW_CONSOLE;
        new_cmdline = malloc((wcslen(dos) + 3 + wcslen(remainder) + 1) * sizeof(WCHAR));
        wcscpy(new_cmdline, L"\"");
        wcscat(new_cmdline, dos);
        wcscat(new_cmdline, L"\"");
        wcscat(new_cmdline, remainder);
        cmdline = new_cmdline;
    }

run:
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    use_shell_execute = should_use_shell_execute(cmdline);
    if (use_shell_execute && wcslen(cmdline) > 10
        && (!memcmp(cmdline, L"link2ea://", 10 * sizeof(WCHAR)) || !memcmp(cmdline, L"steam2ea://", 11 * sizeof(WCHAR))))
    {
        HDESK desktop = GetThreadDesktop(GetCurrentThreadId());
        DWORD is_unavailable, type, size;
        SC_HANDLE manager, service;
        SERVICE_STATUS status;
        DWORD timeout = 3000;
        HKEY eakey;

        link2ea = TRUE;
        /* Valve's Wine keeps the desktop up while EA's app hands over; ours rejects this index and logs it (R14). */
        if (!SetUserObjectInformationA(desktop, 1000, &timeout, sizeof(timeout)))
            ERR("Failed to set desktop timeout, err %lu.\n", GetLastError());
        if (!RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Electronic Arts\\EA Desktop", 0, KEY_ALL_ACCESS, &eakey))
        {
            size = sizeof(is_unavailable);
            if (!RegQueryValueExW(eakey, L"IsUnavailable", NULL, &type, (BYTE *)&is_unavailable, &size)
                && type == REG_DWORD && is_unavailable)
            {
                ERR("EA Desktop\\IsUnavailable is set, clearing.\n");
                is_unavailable = 0;
                RegSetValueExW(eakey, L"IsUnavailable", 0, REG_DWORD, (BYTE *)&is_unavailable, sizeof(is_unavailable));
            }
            RegCloseKey(eakey);
        }
        if ((manager = OpenSCManagerW(NULL, SERVICES_ACTIVE_DATABASEW, SERVICE_QUERY_STATUS)))
        {
            if ((service = OpenServiceW(manager, L"EABackgroundService", SERVICE_QUERY_STATUS)))
            {
                if (QueryServiceStatus(service, &status))
                {
                    if (status.dwCurrentState == SERVICE_STOP_PENDING || status.dwCurrentState == SERVICE_STOPPED)
                        DeleteFileW(L"C:\\ProgramData\\EA Desktop\\backgroundservice.ini");
                }
                else ERR("Could not query service status, error %lu.\n", GetLastError());
                CloseServiceHandle(service);
            }
            CloseServiceHandle(manager);
        }
        else ERR("Could not open service manager, error %lu.\n", GetLastError());
    }
    hide_window = env_nonzero("PROTON_HIDE_PROCESS_WINDOW");

    /* only await the process finishing if we launch a process directly: Steam simply calls ShellExecuteA with the
     * same parameters, and what it starts may ShellExecute something as a throw-away */
    *should_await = !use_shell_execute;

    if (use_shell_execute)
    {
        WCHAR *param = NULL;
        WCHAR *executable_name_end = get_end_of_excutable_name(cmdline);
        unsigned int retry_count = 0;
        INT_PTR ret;

        if (*executable_name_end != '\0')
        {
            *executable_name_end = '\0';
            param = executable_name_end + 1;
        }
        while ((ret = (INT_PTR)ShellExecuteW(NULL, L"open", cmdline, param, NULL, hide_window ? SW_HIDE : SW_SHOWNORMAL)) < 32)
        {
            ERR("Failed to execute %ls, ret %Iu.\n", cmdline, ret);
            if (game_process && ret == SE_ERR_FNF && link2ea)
            {
                if (!try_recover_eadesktop_symlink()) break;
                if (retry_count++) break;
                continue;
            }
            /* Proton uninstalls the EA app here so its prerequisites set it up again on the next run. Install scripts
             * never run on macOS Steam (D-16), so nothing would: it is left alone (R19). */
            if (game_process && ret == SE_ERR_NOASSOC && link2ea)
                ERR("EA app has no link2ea handler; not uninstalling it: nothing would reinstall it.\n");
            break;
        }
        return INVALID_HANDLE_VALUE;
    }
    else
    {
        WCHAR *new_cmdline = fixup_cmdline_paths(cmdline);

        if (hide_window)
        {
            si.dwFlags |= STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
        }
        /* Proton inherits no handles. Then Wine gives the game no standard handles at all, and its own output, which
         * MacNeutron's game log holds (MACNEUTRON_LOG), is lost: we inherit them (D-line). */
        if (!CreateProcessW(NULL, new_cmdline, NULL, NULL, TRUE, flags, NULL, NULL, &si, &pi))
        {
            ERR("could not start %ls (error %lu)\n", new_cmdline, GetLastError());
            return INVALID_HANDLE_VALUE;
        }
        if (new_cmdline != cmdline) free(new_cmdline);
        CloseHandle(pi.hThread);
        return pi.hProcess;
    }
}

/* From here to setup_steam_files: Proton's write_file, escape_path_unix_to_dos, strappend and setup_steam_files. */
static void write_file(const WCHAR *filename, const char *data, size_t len)
{
    DWORD written;
    HANDLE file;

    file = CreateFileW(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) ERR("Could not open %ls.\n", filename);
    else
    {
        if (!WriteFile(file, data, len, &written, NULL)) ERR("Could not write to %ls.\n", filename);
        CloseHandle(file);
    }
}

/* A Unix path as KeyValues text: its DOS path (S: for the game's library), backslashes doubled, UTF-8. */
static char *escape_path_unix_to_dos(const char *path)
{
    WCHAR *dos, *tmp = NULL, *src, *dst;
    char *escaped = NULL;
    UINT len;

    if (!(dos = wine_get_dos_file_name(path)) || !(len = wcslen(dos))) goto done;
    if (!(tmp = malloc((len * 2 + 1) * sizeof(*tmp)))) goto done;
    for (src = dos, dst = tmp; *src; src++, dst++) if ((*dst = *src) == '\\') *++dst = '\\';
    if (!(len = WideCharToMultiByte(CP_UTF8, 0, tmp, (dst - tmp), NULL, 0, NULL, NULL))) goto done;
    if ((escaped = malloc(len + 1)))
    {
        WideCharToMultiByte(CP_UTF8, 0, tmp, (dst - tmp), escaped, len, NULL, NULL);
        escaped[len] = '\0';
    }
done:
    HeapFree(GetProcessHeap(), 0, dos);
    free(tmp);
    return escaped;
}

static size_t strappend(char **buf, size_t *buf_size, size_t pos, const char *fmt, ...)
{
    size_t size;
    va_list ap;
    char *ptr;
    int n;

    if (*buf)
    {
        size = *buf_size;
        ptr = *buf;
    }
    else
    {
        size = 100;
        ptr = malloc(size);
    }
    for (;;)
    {
        va_start(ap, fmt);
        n = vsnprintf(ptr + pos, size - pos, fmt, ap);
        va_end(ap);
        if (n == -1) size *= 2;
        else if (pos + (size_t)n >= size) size = pos + n + 1;
        else break;
        ptr = realloc(ptr, size);
    }
    *buf_size = size;
    *buf = ptr;
    return n;
}

/* Steam's config folder, which CEG looks for, and steamapps\libraryfolders.vdf, which the 2K Launcher and mod managers
 * read: one entry per STEAM_COMPAT_LIBRARY_PATHS item, its steamapps folder's parent, as the game sees it. */
static void setup_steam_files(void)
{
    char *steam_library_paths;
    const char *start, *end, *next;
    size_t buf_size = 0, pos = 0;
    char *buf = NULL, *str;
    unsigned int index = 0;

    if (!CreateDirectoryW(STEAM_DIR L"\\config", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        ERR("Failed to create config directory, GetLastError() %lu.\n", GetLastError());
        return;
    }
    if (!CreateDirectoryW(STEAM_DIR L"\\steamapps", NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
    {
        ERR("Failed to create steamapps directory, GetLastError() %lu.\n", GetLastError());
        return;
    }
    steam_library_paths = getenv_unix(L"STEAM_COMPAT_LIBRARY_PATHS");
    pos += strappend(&buf, &buf_size, pos, "\"LibraryFolders\"\n{\n");
    /* Proton lists STEAM_COMPAT_CLIENT_INSTALL_PATH first; on macOS that is the Steam bundle's MacOS folder, which holds
     * no library (D-8). */
    start = steam_library_paths;
    while (start && *start)
    {
        char *path;

        if (!(next = strchr(start, ':'))) next = start + strlen(start);
        end = next;
        if (end != start && end[-1] == '/') --end;
        while (end != start && end[-1] != '/') --end;
        /* An empty item ("a::b") or one without a /: Proton copies nothing into an uninitialised buffer there. */
        if (end != start)
        {
            path = malloc(end - start + 1);
            lstrcpynA(path, start, end - start);
            if (!(str = escape_path_unix_to_dos(path)))
            {
                WCHAR wpath[MAX_PATH];

                if (!MultiByteToWideChar(CP_UNIXCP, 0, path, -1, wpath, ARRAYSIZE(wpath))) wcscpy(wpath, L"(a long path)");
                ERR("Could not convert %ls to win path.\n", wpath);
            }
            else
            {
                pos += strappend(&buf, &buf_size, pos, "\t\"%u\"\n\t{\n\t\t\"path\"\t\t\"%s\"\n\t}\n", index, str);
                ++index;
                free(str);
            }
            free(path);
        }
        if (*next == ':') ++next;
        start = next;
    }
    pos += strappend(&buf, &buf_size, pos, "}\n");
    write_file(STEAM_DIR L"\\steamapps\\libraryfolders.vdf", buf, pos);
    free(buf);
    free(steam_library_paths);
}

/* Proton's dummy Steam windows, which games look for with FindWindow (hidden: WS_POPUP, never shown). */
static DWORD WINAPI create_steam_windows(void *arg)
{
    static WNDCLASSEXW wndclass = { sizeof(WNDCLASSEXW) };
    MSG msg;

    wndclass.lpfnWndProc = DefWindowProcW;
    wndclass.lpszClassName = L"vguiPopupWindow";
    RegisterClassExW(&wndclass);
    CreateWindowW(wndclass.lpszClassName, L"Steam", WS_POPUP, 40, 40, 400, 300, NULL, NULL, NULL, NULL);
    CreateWindowA("static", "SteamVR Status", WS_POPUP, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

/* Proton's setup_vr_registry: our runtime has no vrclient, so this logs and goes on. */
static void setup_vr_registry(void)
{
    BOOL (CDECL *init)(void);
    HMODULE vrclient;

    if (!(vrclient = LoadLibraryW(L"vrclient_x64")))
    {
        ERR("Failed to load vrclient module, skipping initialization\n");
        return;
    }
    if ((init = (void *)GetProcAddress(vrclient, "vrclient_init_registry"))) init();
    else ERR("Failed to find vrclient_init_registry export\n");
    FreeLibrary(vrclient);
}

/* Valve's Wine reports a Unix debugger (class 1100, Linux only); ours has no such class, so on a Mac only a Windows
 * debugger (winedbg) ends PROTON_WAIT_ATTACH's wait. */
static BOOL is_ptraced(void)
{
    ULONG len;
    int pid;

    if (NtQueryInformationProcess(GetCurrentProcess(), (PROCESSINFOCLASS)1100 /* ProcessWineUnixDebuggerPid */,
                                  &pid, sizeof(pid), &len))
        return FALSE;
    return !!pid;
}

/* A steam:// URL to Mac Steam: winebrowser, which runs open(1) (D-12; Proton runs the Linux client's own commands,
 * steam-runtime-steam-remote or steam, which macOS has no equivalent of). Never ShellExecute: HKCR\steam is us. */
static void forward_to_mac_steam(const WCHAR *url)
{
    static const WCHAR format[] = L"C:\\windows\\system32\\winebrowser.exe \"%ls\"";
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    size_t len = wcslen(url) + ARRAYSIZE(format);
    WCHAR *cmd = malloc(len * sizeof(*cmd));

    if (!cmd)
    {
        ERR("Out of memory forwarding %ls to Mac Steam.\n", url);
        return;
    }
    _snwprintf(cmd, len, format, url);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        ERR("Forwarding %ls to Mac Steam failed, error %lu.\n", url, GetLastError());
    else
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    free(cmd);
}

/* An argument as a Windows command line spells it, which CommandLineToArgvW reads back: quoted when it is empty or holds
 * a space, tab or quote; \" for a quote, and backslashes doubled before one or before the closing quote. malloc'd. */
static WCHAR *spell_argument(const WCHAR *arg)
{
    size_t len = wcslen(arg), i, n = 0, slashes = 0;
    BOOL quote = !len || wcspbrk(arg, L" \t\"");
    WCHAR *ret = malloc((2 * len + 3) * sizeof(*ret));

    if (!ret) return NULL;
    if (quote) ret[n++] = '"';
    for (i = 0; i < len; ++i)
    {
        if (arg[i] == '"')
        {
            for (; slashes; --slashes) ret[n++] = '\\';
            ret[n++] = '\\';
        }
        slashes = arg[i] == '\\' ? slashes + 1 : 0;
        ret[n++] = arg[i];
    }
    if (quote)
    {
        for (; slashes; --slashes) ret[n++] = '\\';
        ret[n++] = '"';
    }
    ret[n] = 0;
    return ret;
}

/* -applaunch <id> [arguments]: Proton passes them to the Linux client's command line. Mac Steam has none, so the URL
 * carries them: steam://rungameid/<id>, or Valve's steam://run/<id>//<arguments>/ with the arguments spelled as a
 * Windows command line and percent-encoded as UTF-8. Encoded, a '/', '?', '#' or '%' in an argument can't change the
 * URL's structure (Steam splits it on '/'); the form is common usage, as in steam://run/730//+connect%20<server>. */
static void forward_applaunch(WCHAR **args, int count)
{
    static const char keep[] = "-_.~+:,=@!$'()*;&";
    size_t size = 0, pos = 0;
    char *url = NULL, *utf8;
    WCHAR *wide, *spelled;
    int i, j, r;

    pos += strappend(&url, &size, pos, count > 1 ? "steam://run/" : "steam://rungameid/");
    for (i = 0; i < count; ++i)
    {
        const WCHAR *text;

        spelled = i ? spell_argument(args[i]) : NULL;  /* the ID goes as it is */
        text = i ? spelled : args[i];

        if (i == 1) pos += strappend(&url, &size, pos, "//");
        else if (i) pos += strappend(&url, &size, pos, "%%20");
        if (!text || !(r = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL)) || !(utf8 = malloc(r)))
        {
            ERR("Can't convert %ls for Mac Steam.\n", args[i]);
            free(spelled);
            free(url);
            return;
        }
        WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, r, NULL, NULL);
        for (j = 0; utf8[j]; ++j)
        {
            unsigned char c = utf8[j];

            if (c < 0x80 && (isalnum(c) || strchr(keep, c))) pos += strappend(&url, &size, pos, "%c", c);
            else pos += strappend(&url, &size, pos, "%%%02X", c);
        }
        free(utf8);
        free(spelled);
    }
    if (count > 1) pos += strappend(&url, &size, pos, "/");
    if ((r = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0)) && (wide = malloc(r * sizeof(*wide))))
    {
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, r);
        forward_to_mac_steam(wide);
        free(wide);
    }
    else ERR("Out of memory forwarding %hs to Mac Steam.\n", url);
    free(url);
}

/* Proton's steam_command_handler: steam:// URLs and options are commands, anything else is a program to start. */
static BOOL steam_command_handler(int argc, char *argv[])
{
    const char *sgi;
    WCHAR **wargv;
    int i = 1, wargc;

    /* If there are command line options, only forward steam:// and options start with - */
    if (argc > 1 && StrStrIA(argv[1], "steam://") != argv[1] && argv[1][0] != '-') return FALSE;
    /* The game restarting itself (HKCR's form: -- steam://launch/<id> or steam://rungameid/<id>): signal the steam.exe
     * that started it. Proton opens the event for SYNCHRONIZE only, which our Wine refuses SetEvent (R6). */
    if (argc > 2 && !strcmp(argv[1], "--") && (sgi = getenv("SteamGameId")))
    {
        char launch[64], rungameid[64];

        snprintf(launch, sizeof(launch), "steam://launch/%s", sgi);
        snprintf(rungameid, sizeof(rungameid), "steam://rungameid/%s", sgi);
        if (!strcmp(argv[2], launch) || !strcmp(argv[2], rungameid))
        {
            HANDLE event = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, L"PROTON_STEAM_EXE_RESTART_APP");

            if (event)
            {
                SetEvent(event);
                CloseHandle(event);
            }
            else ERR("Restart event not found.\n");
            return TRUE;
        }
    }
    /* Mac Steam has no command line (D-12): HKCR's -- goes, URLs are forwarded, -applaunch <id> and what follows it
     * become a URL, nothing at all opens Steam, and every other option is dropped. From the wide command line: the C
     * runtime's ANSI argv turns letters outside the Windows code page into '?'. */
    if (!(wargv = CommandLineToArgvW(GetCommandLineW(), &wargc)))
    {
        ERR("Can't read the command line, error %lu.\n", GetLastError());
        return TRUE;
    }
    if (wargc > 1 && !wcscmp(wargv[1], L"--")) i = 2;
    if (i >= wargc) forward_to_mac_steam(L"steam://open/main");
    for (; i < wargc; ++i)
    {
        if (StrStrIW(wargv[i], L"steam://") == wargv[i]) forward_to_mac_steam(wargv[i]);
        else if (!wcscmp(wargv[i], L"-applaunch") && i + 1 < wargc)
        {
            forward_applaunch(wargv + i + 1, wargc - i - 1);
            break;
        }
        else ERR("ignoring %ls (Mac Steam has no command line)\n", wargv[i]);
    }
    LocalFree(wargv);
    return TRUE;
}

/* Proton's main, in its order: Steam is set up only for a game process (SteamGameId set, empty included). */
int main(int argc, char *argv[])
{
    HANDLE wait_handle = INVALID_HANDLE_VALUE, child = INVALID_HANDLE_VALUE;
    BOOL game_process = FALSE;
    BOOL should_await = TRUE;  /* hardening: Proton's run_process can return before setting it */
    DWORD rc = 0;

    if (steam_command_handler(argc, argv)) return 0;  /* every run with no arguments is one */
    if (getenv("SteamGameId"))
    {
        WCHAR path[MAX_PATH], *p;

        /* Named objects the Windows client holds: Denuvo checks Steam3Master_SharedMemLock (Just Cause 3 deletes its
         * saves without it), the 2K Launcher Global\Valve_SteamIPC_Class. Ours last until we exit. */
        CreateEventW(NULL, FALSE, FALSE, L"Steam3Master_SharedMemLock");
        CreateEventW(NULL, FALSE, FALSE, L"Global\\Valve_SteamIPC_Class");
        /* The desktop window first, on this thread: win32u hands explorer our environment unlocked, which the
         * SetEnvironmentVariableW calls below would race (Proton 42f903ef). */
        GetDesktopWindow();
        CreateThread(NULL, 0, create_steam_windows, NULL, 0, NULL);
        register_steam(GetCurrentProcessId());
        /* Steam on Windows sets these for its games (Source's GameUI finds Steam through ValvePlatformMutex): the
         * path of the steam.exe that ran, with / for \. The game copies our environment when it starts. */
        SetEnvironmentVariableW(L"SteamPath", STEAM_DIR);
        *path = 0;
        GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
        for (p = path; *p; ++p) if (*p == '\\') *p = '/';
        SetEnvironmentVariableW(L"ValvePlatformMutex", path);
        setup_steam_files();
        if (env_nonzero("PROTON_WAIT_ATTACH"))
            while (!IsDebuggerPresent() && !is_ptraced()) Sleep(100);
        game_process = TRUE;
    }
    if (game_process)
    {
        setup_vr_registry();
        /* Before the game runs, so its stub finds the semaphores (Proton creates them once the game has started: R5). */
        drm_consume = CreateSemaphoreW(NULL, 0, 512, L"STEAM_DIPC_CONSUME");
        drm_produce = CreateSemaphoreW(NULL, 1, 512, L"SREAM_DIPC_PRODUCE");
        if (drm_consume && drm_produce) CreateThread(NULL, 0, answer_drm, NULL, 0, NULL);
    }
    child = run_process(&should_await, game_process);
    if (should_await)
    {
        if (child == INVALID_HANDLE_VALUE) return 1;
        wait_handle = child;
    }
    /* A game process waits for Wine's shutdown event, set once no program is left in the prefix: it covers a launcher
     * that starts the real game, even with CREATE_BREAKAWAY_FROM_JOB or ShellExecute, and quits. A system process
     * doesn't keep the prefix alive itself. If Wine refuses, wait_handle stays the program, or nothing. */
    if (game_process)
        NtSetInformationProcess(GetCurrentProcess(), (PROCESSINFOCLASS)1000 /* ProcessWineMakeProcessSystem */,
                                &wait_handle, sizeof(wait_handle));
    if (wait_handle != INVALID_HANDLE_VALUE)
    {
        HANDLE waits[2] = { wait_handle, NULL };
        DWORD ret, wait_count = 1;

        /* The game asks for its own restart through steam://rungameid/<its id> (steam_command_handler). */
        if (game_process)
        {
            if ((waits[1] = CreateEventW(NULL, FALSE, FALSE, L"PROTON_STEAM_EXE_RESTART_APP"))) ++wait_count;
            else ERR("Failed to create restart event, err %lu.\n", GetLastError());
        }
        FreeConsole();
        /* Proton's loop, without its precedence slip (ret got the comparison, so a failed wait read as a restart): it
         * loops only on the restart event (R7). */
        while ((ret = WaitForMultipleObjects(wait_count, waits, FALSE, INFINITE)) != WAIT_OBJECT_0)
        {
            if (ret != WAIT_OBJECT_0 + 1)
            {
                ERR("Wait failed.\n");
                break;
            }
            if (child != INVALID_HANDLE_VALUE)
            {
                if (WaitForSingleObject(child, 0) == WAIT_TIMEOUT)
                {
                    ERR("Child is still running, not restarting.\n");
                    continue;
                }
                CloseHandle(child);
            }
            child = run_process(&should_await, game_process);
        }
        if (waits[1]) CloseHandle(waits[1]);
    }
    if (child != INVALID_HANDLE_VALUE)
    {
        GetExitCodeProcess(child, &rc);
        CloseHandle(child);
    }
    return (int)rc;
}
