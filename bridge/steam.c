/* steam.exe for MacNeutron: tells Windows games inside Wine that Steam is running.
 *
 *   steam.exe <program> [arguments...]      (<program> is a Windows path)
 *
 * steam_api(64).dll treats Steam as running when HKCU\Software\Valve\Steam\ActiveProcess\pid names a
 * live process, and loads the client DLL named next to it. This writes those values (pointing at the
 * runtime's Steam bridge, which the launcher copies into C:\Program Files (x86)\Steam), runs <program>,
 * answers SteamStub DRM's start check (see answer_drm), waits until no program is left in the prefix
 * (launchers start the real game and quit), clears the pid, and exits with <program>'s exit code.
 * Spec: docs/superpowers/specs/2026-09-28-macneutron-steam-bridge-design.md §4.2.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define STEAM_DIR L"C:\\Program Files (x86)\\Steam"
#define STEAM_KEY L"Software\\Valve\\Steam"
#define ACTIVE_PROCESS_KEY STEAM_KEY L"\\ActiveProcess"

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

static void register_steam(DWORD pid)
{
    WCHAR account[16];
    HKEY key;

    if ((key = create_key(HKEY_CURRENT_USER, STEAM_KEY)))
    {
        set_string(key, L"SteamPath", STEAM_DIR);
        set_string(key, L"SteamExe", STEAM_DIR L"\\steam.exe");
        RegCloseKey(key);
    }
    if ((key = create_key(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY)))
    {
        set_dword(key, L"pid", pid);
        set_string(key, L"SteamClientDll64", STEAM_DIR L"\\steamclient64.dll");
        if (GetFileAttributesW(STEAM_DIR L"\\steamclient.dll") != INVALID_FILE_ATTRIBUTES)
            set_string(key, L"SteamClientDll", STEAM_DIR L"\\steamclient.dll");
        if (GetEnvironmentVariableW(L"MACNEUTRON_STEAM_ACCOUNT", account, ARRAYSIZE(account)))
            set_dword(key, L"ActiveUser", wcstoul(account, NULL, 10));
        RegCloseKey(key);
    }
    if ((key = create_key(HKEY_LOCAL_MACHINE, L"Software\\Wow6432Node\\Valve\\Steam")))
    {
        set_string(key, L"InstallPath", STEAM_DIR);
        RegCloseKey(key);
    }
}

/* Another steam.exe in the same prefix may own the pid by now: only clear our own. Every steam.exe ends once nothing
 * runs in the prefix, so none is left to hand it back to; only one that Wine refused the shutdown event ends early. */
static void unregister_steam(DWORD pid)
{
    DWORD current = 0, size = sizeof(current);
    HKEY key;

    if (!(key = create_key(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY))) return;
    if (!RegQueryValueExW(key, L"pid", NULL, NULL, (BYTE *)&current, &size) && current == pid)
        set_dword(key, L"pid", 0);
    RegCloseKey(key);
}

/* ntdll's object-directory calls, which mingw's headers leave out. */
typedef struct { UNICODE_STRING ObjectName, ObjectTypeName; } DIRECTORY_BASIC_INFORMATION;
NTSTATUS NTAPI NtOpenDirectoryObject(HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *);
NTSTATUS NTAPI NtQueryDirectoryObject(HANDLE, void *, ULONG, BOOLEAN, BOOLEAN, ULONG *, ULONG *);

/* SteamStub DRM (Steam's wrapper on many games' exes) has Steam confirm the start: it takes a slot from the
 * SREAM_DIPC_PRODUCE semaphore (Valve's spelling), creates an event named STEAM_START_ACK_EVENT<something>, signals
 * STEAM_DIPC_CONSUME and waits for Steam to set the event. Unanswered, the game stops with "Application load error
 * 3:0000065432". This answers it the way Proton's steam.exe does (steam_helper/steam.c, steam_drm_thread). */
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

/* Our command line minus our own name (argv[0] rules: quoted, or up to the first blank). */
static WCHAR *child_command_line(void)
{
    const WCHAR *p = GetCommandLineW();

    if (*p == '"')
    {
        for (p++; *p && *p != '"'; p++) ;
        if (*p) p++;
    }
    else
        while (*p && *p != ' ' && *p != '\t') p++;
    while (*p == ' ' || *p == '\t') p++;
    return *p ? _wcsdup(p) : NULL;
}

int main(void)
{
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DWORD pid = GetCurrentProcessId(), code = 1;
    WCHAR *cmd = child_command_line();
    HANDLE done;

    if (!cmd)
    {
        fprintf(stderr, "usage: steam.exe <program> [arguments...]\n");
        return 1;
    }
    /* Proton's steam.exe sets these for the game, as Steam on Windows does (Source's GameUI finds Steam through
     * ValvePlatformMutex); macOS Steam doesn't. Before CreateProcessW, which copies our environment. */
    SetEnvironmentVariableW(L"SteamPath", STEAM_DIR);
    /* ponytail: a constant; Proton's GetModuleFileNameW would repeat the command line's letter case. */
    SetEnvironmentVariableW(L"ValvePlatformMutex", L"C:/Program Files (x86)/Steam/steam.exe");
    /* Registered only once the child exists (still suspended): a steam.exe that can't start its
     * program must not touch the Steam another one registered. */
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL, &si, &pi))
    {
        fprintf(stderr, "steam.exe: could not start %ls (error %lu)\n", cmd, GetLastError());
        return 1;
    }
    register_steam(pid);
    /* Before the game runs, so its stub finds the semaphores. */
    drm_consume = CreateSemaphoreW(NULL, 0, 512, L"STEAM_DIPC_CONSUME");
    drm_produce = CreateSemaphoreW(NULL, 1, 512, L"SREAM_DIPC_PRODUCE");
    if (drm_consume && drm_produce) CreateThread(NULL, 0, answer_drm, NULL, 0, NULL);
    ResumeThread(pi.hThread);
    /* Wine's shutdown event, set once no program is left in the prefix, as Proton's steam.exe waits (after the program
     * started): unlike the program's handle or a job, it covers a launcher that starts the real game, even with
     * CREATE_BREAKAWAY_FROM_JOB or ShellExecute, and quits. A system process doesn't keep the prefix alive itself. If
     * Wine refuses, `done` stays the program. */
    done = pi.hProcess;
    NtSetInformationProcess(GetCurrentProcess(), (PROCESSINFOCLASS)1000 /* ProcessWineMakeProcessSystem */, &done,
                            sizeof(done));
    /* Under a terminal Wine starts a console host for us, which would wait for us to leave while we wait for it. */
    FreeConsole();
    WaitForSingleObject(done, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    unregister_steam(pid);
    return (int)code;
}
