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

static void read_string(HKEY root, const WCHAR *subkey, REGSAM sam, const WCHAR *name, WCHAR *out, DWORD cap_chars)
{
    HKEY key;
    DWORD type = 0, bytes = (cap_chars - 1) * sizeof(WCHAR);

    out[0] = 0;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE | sam, &key)) return;
    if (RegQueryValueExW(key, name, NULL, &type, (BYTE *)out, &bytes) || type != REG_SZ) out[0] = 0;
    else out[bytes / sizeof(WCHAR)] = 0;
    RegCloseKey(key);
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
        printf("alive=%d client64=", steam_alive());
        put(client);
        fputs("\n", stdout);
        return 0;
    }
    if (!wcscmp(mode, L"reg"))  /* 64-bit and Wow6432Node InstallPath + ActiveProcess Universe and SteamPath */
    {
        WCHAR install64[MAX_PATH], install32[MAX_PATH], universe[64], steampath[MAX_PATH];

        read_string(HKEY_LOCAL_MACHINE, L"Software\\Valve\\Steam", KEY_WOW64_64KEY, L"InstallPath",
                    install64, ARRAYSIZE(install64));
        read_string(HKEY_LOCAL_MACHINE, L"Software\\Wow6432Node\\Valve\\Steam", 0, L"InstallPath",
                    install32, ARRAYSIZE(install32));
        read_string(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY, 0, L"Universe", universe, ARRAYSIZE(universe));
        read_string(HKEY_CURRENT_USER, ACTIVE_PROCESS_KEY, 0, L"SteamPath", steampath, ARRAYSIZE(steampath));
        fputs("install64=", stdout); put(install64);
        fputs(" install32=", stdout); put(install32);
        fputs(" universe=", stdout); put(universe);
        fputs(" steampath=", stdout); put(steampath);
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
    if (!wcscmp(mode, L"app") && argc > 2)  /* HKCU\Software\Valve\Steam\Apps\<id> and Steam's language; 9 is missing */
    {
        WCHAR key[64], language[64] = L"";
        DWORD installed = 9, running = 9, updating = 9, size;

        _snwprintf(key, ARRAYSIZE(key), L"Software\\Valve\\Steam\\Apps\\%ls", argv[2]);
        size = sizeof(DWORD); RegGetValueW(HKEY_CURRENT_USER, key, L"Installed", RRF_RT_REG_DWORD, NULL, &installed, &size);
        size = sizeof(DWORD); RegGetValueW(HKEY_CURRENT_USER, key, L"Running", RRF_RT_REG_DWORD, NULL, &running, &size);
        size = sizeof(DWORD); RegGetValueW(HKEY_CURRENT_USER, key, L"Updating", RRF_RT_REG_DWORD, NULL, &updating, &size);
        size = sizeof(language);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"language", RRF_RT_REG_SZ, NULL, language, &size);
        printf("installed=%lu running=%lu updating=%lu language=", installed, running, updating);
        put(language);
        fputs("\n", stdout);
        return 0;
    }
    if (!wcscmp(mode, L"url") && argc > 2)  /* a game opening a steam:// link, as an in-game store button does */
    {
        INT_PTR ret = (INT_PTR)ShellExecuteW(NULL, L"open", argv[2], NULL, NULL, SW_SHOWNORMAL);
        printf(ret > 32 ? "url ok\n" : "url failed (%d)\n", (int)ret);
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
    /* A game restarting itself through Steam (steam://rungameid/<its id>): counts its starts in <file>; the first start
     * asks for the restart once steam.exe's restart event exists, then exits; the next exits with <code>. */
    if (!wcscmp(mode, L"restart") && argc > 3)
    {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        WCHAR cmd[256], id[32] = L"";
        HANDLE event;
        FILE *f;
        int n = 0;

        if ((f = _wfopen(argv[2], L"r"))) { if (fscanf(f, "%d", &n) != 1) n = 0; fclose(f); }
        if (!(f = _wfopen(argv[2], L"w"))) return 2;
        fprintf(f, "%d\n", ++n);
        fclose(f);
        if (n > 1) return _wtoi(argv[3]);
        for (i = 0; i < 20; i++)
        {
            if ((event = OpenEventW(SYNCHRONIZE, FALSE, L"PROTON_STEAM_EXE_RESTART_APP"))) { CloseHandle(event); break; }
            Sleep(100);
        }
        GetEnvironmentVariableW(L"SteamGameId", id, ARRAYSIZE(id));
        _snwprintf(cmd, ARRAYSIZE(cmd), L"\"C:\\Program Files (x86)\\Steam\\steam.exe\" -- steam://rungameid/%ls", id);
        if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        return 0;
    }
    if (!wcscmp(mode, L"window"))  /* Steam's hidden window, which games look for with FindWindow */
    {
        int i;

        for (i = 0; i < 20 && !FindWindowW(L"vguiPopupWindow", L"Steam"); i++) Sleep(100);
        printf("window=%d\n", FindWindowW(L"vguiPopupWindow", L"Steam") != NULL);
        return 0;
    }
    if (!wcscmp(mode, L"module"))  /* the path the game sees itself at (S: in a Steam library) */
    {
        WCHAR path[MAX_PATH];

        GetModuleFileNameW(NULL, path, ARRAYSIZE(path));
        put(path);
        fputs("\n", stdout);
        return 0;
    }
    /* Built as a console program (helper-console.exe): whether steam.exe gave it a console of its own without
     * inheriting the parent's redirected stdout pipe. Its stdout doesn't reach check.sh, so it writes to <file>. */
    if (!wcscmp(mode, L"console") && argc > 2)
    {
        DWORD list[8], cmode = 0;
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD type = GetFileType(h);
        FILE *out;

        fputs("leaked_stdout\n", stdout);
        fflush(stdout);
        if (!(out = _wfopen(argv[2], L"w"))) return 2;
        fprintf(out, "own_console=%d pipe_stdout=%d console_stdout=%d\n",
                GetConsoleProcessList(list, ARRAYSIZE(list)) == 1,
                type == FILE_TYPE_PIPE,
                GetConsoleMode(h, &cmode) != 0);
        fclose(out);
        return 0;
    }
    if (!wcscmp(mode, L"marker") && argc > 2)  /* a batch file's proof that it ran */
    {
        FILE *out;

        if (!(out = _wfopen(argv[2], L"w"))) return 2;
        fputs("marker\n", out);
        fclose(out);
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
