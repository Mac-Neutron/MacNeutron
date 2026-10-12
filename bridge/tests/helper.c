/* Test program for bridge/check.sh: runs under steam.exe and reports what it sees. */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define ACTIVE_PROCESS_KEY L"Software\\Valve\\Steam\\ActiveProcess"

static void put(const WCHAR *s)
{
    char buf[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, buf, sizeof(buf), NULL, NULL)) fputs(buf, stdout);
}

static DWORD read_dword(const WCHAR *name)
{
    DWORD value = 0, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY, name, RRF_RT_REG_DWORD, NULL, &value, &size);
    return value;
}

/* 1 when ActiveProcess\pid names a running process: the check steam_api.dll makes. */
static int steam_alive(void)
{
    DWORD pid = read_dword(L"pid"), code = 0;
    HANDLE process;

    if (!pid || !(process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))) return 0;
    GetExitCodeProcess(process, &code);
    CloseHandle(process);
    return code == STILL_ACTIVE;
}

int main(void)
{
    int argc, i;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const WCHAR *mode = argc > 1 ? argv[1] : L"";

    if (!wcscmp(mode, L"exit") && argc > 2) return _wtoi(argv[2]);
    if (!wcscmp(mode, L"args"))
    {
        for (i = 2; i < argc; i++) { fputs("[", stdout); put(argv[i]); fputs("]", stdout); }
        fputs("\n", stdout);
        return 0;
    }
    if (!wcscmp(mode, L"steam"))
    {
        WCHAR client[MAX_PATH] = L"";
        DWORD size = sizeof(client);
        RegGetValueW(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY, L"SteamClientDll64", RRF_RT_REG_SZ, NULL, client, &size);
        printf("alive=%d user=%lu client64=", steam_alive(), read_dword(L"ActiveUser"));
        put(client);
        fputs("\n", stdout);
        return 0;
    }
    if (!wcscmp(mode, L"pid"))
    {
        printf("%lu\n", read_dword(L"pid"));
        return 0;
    }
    if (!wcscmp(mode, L"spawn") && argc > 2)  /* a launcher: start the real game, then quit */
    {
        WCHAR self[MAX_PATH], cmd[3 * MAX_PATH];
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        GetModuleFileNameW(NULL, self, MAX_PATH);
        _snwprintf(cmd, ARRAYSIZE(cmd), L"\"%ls\" late \"%ls\"", self, argv[2]);
        cmd[ARRAYSIZE(cmd) - 1] = 0;
        return CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi) ? 0 : 2;
    }
    if (!wcscmp(mode, L"env"))  /* the variables steam.exe sets for the game */
    {
        for (i = 2; i < argc; i++)
        {
            const WCHAR *value = _wgetenv(argv[i]);
            fputs("[", stdout); put(value ? value : L""); fputs("]", stdout);
        }
        fputs("\n", stdout);
        return 0;
    }
    if (!wcscmp(mode, L"hidden"))  /* asked to start without showing a window */
    {
        STARTUPINFOW si;
        GetStartupInfoW(&si);
        printf("hidden=%d\n", (si.dwFlags & STARTF_USESHOWWINDOW) && si.wShowWindow == SW_HIDE);
        return 0;
    }
    if (!wcscmp(mode, L"events"))  /* named objects the Windows client holds */
    {
        HANDLE lock = OpenEventW(SYNCHRONIZE, FALSE, L"Steam3Master_SharedMemLock");
        HANDLE ipc = OpenEventW(SYNCHRONIZE, FALSE, L"Global\\Valve_SteamIPC_Class");
        printf("lock=%d ipc=%d\n", !!lock, !!ipc);
        return 0;
    }
    if (!wcscmp(mode, L"breakaway") && argc > 2)  /* a launcher that starts the real game outside any job, then quits */
    {
        WCHAR self[MAX_PATH], cmd[3 * MAX_PATH];
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;

        GetModuleFileNameW(NULL, self, MAX_PATH);
        _snwprintf(cmd, ARRAYSIZE(cmd), L"\"%ls\" late \"%ls\"", self, argv[2]);
        cmd[ARRAYSIZE(cmd) - 1] = 0;
        if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &pi))
        {
            printf("breakaway failed (error %lu)\n", GetLastError());
            return 2;
        }
        printf("breakaway ok\n");
        return 0;
    }
    if (!wcscmp(mode, L"drm"))  /* SteamStub DRM's start handshake with Steam, as Proton's steam.exe answers it */
    {
        WCHAR name[64];
        HANDLE ack, produce, consume;

        _snwprintf(name, ARRAYSIZE(name), L"STEAM_START_ACK_EVENT_%lu", GetCurrentProcessId());
        ack = CreateEventW(NULL, FALSE, FALSE, name);
        produce = OpenSemaphoreW(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, L"SREAM_DIPC_PRODUCE");
        consume = OpenSemaphoreW(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, L"STEAM_DIPC_CONSUME");
        if (!ack || !produce || !consume)
        {
            printf("no Steam DRM channel\n");
            return 0;
        }
        if (WaitForSingleObject(produce, 5000)) { printf("no slot\n"); return 0; }
        ReleaseSemaphore(consume, 1, NULL);
        printf("ack=%d\n", WaitForSingleObject(ack, 5000) == WAIT_OBJECT_0);
        return 0;
    }
    if (!wcscmp(mode, L"late") && argc > 2)  /* the real game, still running after its launcher quit */
    {
        FILE *out;

        Sleep(1500);
        if (!(out = _wfopen(argv[2], L"w"))) return 2;
        fprintf(out, "alive=%d\n", steam_alive());
        fclose(out);
        return 0;
    }
    fprintf(stderr, "helper: unknown mode\n");
    return 3;
}
