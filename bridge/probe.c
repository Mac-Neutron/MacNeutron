/* steamprobe.exe: checks the Steam bridge end to end through a game's own steam_api64.dll.
 * Run it with bridge/probe.sh, which prepares a prefix like the launcher does (SteamAppId=480).
 *   steamprobe.exe <Windows path of steam_api64.dll> [fault]
 * Uses only the DLL's flat C exports, so no Steamworks headers are needed.
 * Exits 0 only when init, the SteamID, the persona name and an auth-ticket callback all work. With "fault", it then
 * raises an access violation and catches it with SEH (ship-base spec §7): Steam's own crash handler, set up by
 * SteamAPI_Init, must leave the process's faults to Wine. Build with -fms-extensions (__try). */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { int32_t user; int32_t id; uint8_t *param; int32_t size; } CallbackMsg;  /* CallbackMsg_t */
enum { GET_AUTH_SESSION_TICKET_RESPONSE = 163 };                                          /* k_iSteamUserCallbacks + 63 */

static HMODULE api;

/* NULL, read at run time: the compiler can't turn the store below into a trap of its own. */
static volatile int *volatile nowhere;

/* Clang's __try only covers faults at call sites, so the store sits in its own function. */
static __declspec(noinline) void store_through_null(void) { *nowhere = 1; }

static void *find(const char *name) { return (void *)GetProcAddress(api, name); }

#ifdef __i386__
/* i686 MinGW clang has no __try: steamprobe32.exe catches the same fault with a vectored handler, which ends the run. */
static int fault_status;
static LONG CALLBACK caught(EXCEPTION_POINTERS *e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) return EXCEPTION_CONTINUE_SEARCH;
    printf("fault: caught\n");
    fflush(stdout);
    ExitProcess(fault_status);
}
#endif

#define NEED(var, name) \
    if (!(*(void **)&(var) = find(name))) { printf("missing export %s\n", name); return 1; }

/* The newest SteamAPI_<iface>_v0NN accessor the DLL exports, called. */
static void *accessor(const char *iface)
{
    char name[64];
    int version;

    for (version = 40; version > 0; version--)
    {
        void *(*get)(void);
        snprintf(name, sizeof(name), "SteamAPI_%s_v%03d", iface, version);
        if ((*(void **)&get = find(name)))
        {
            printf("%s: %s\n", iface, name);
            return get();
        }
    }
    printf("%s: no accessor\n", iface);
    return NULL;
}

int main(int argc, char **argv)
{
    uint8_t (*init)(void);
    int (*init_flat)(char *);
    void (*dispatch_init)(void);
    int32_t (*get_pipe)(void);
    void (*run_frame)(int32_t);
    uint8_t (*next_callback)(int32_t, CallbackMsg *);
    void (*free_callback)(int32_t);
    uint64_t (*get_steam_id)(void *);
    const char *(*persona)(void *);
    uint32_t (*get_ticket)(void *, void *, int, uint32_t *, void *);  /* SDK >= 1.58 adds the last argument */
    void *user, *friends;
    uint8_t ticket[1024];
    uint32_t ticket_size = 0, handle;
    uint64_t steam_id;
    const char *name;
    int ok, got_ticket = 0, i;

    if (argc < 2) { fprintf(stderr, "usage: steamprobe.exe <steam_api64.dll>\n"); return 2; }
    if (!(api = LoadLibraryA(argv[1]))) { printf("load: FAIL (error %lu)\n", GetLastError()); return 1; }

    if ((*(void **)&init = find("SteamAPI_Init"))) ok = init();
    else
    {
        char message[1024] = "";
        NEED(init_flat, "SteamAPI_InitFlat");
        ok = init_flat(message) == 0;
        if (!ok) printf("init message: %s\n", message);
    }
    printf("init: %s\n", ok ? "ok" : "FAIL");
    if (!ok) return 1;

    NEED(dispatch_init, "SteamAPI_ManualDispatch_Init");
    dispatch_init();  /* after SteamAPI_Init, as steam_api.h requires */
    NEED(get_pipe, "SteamAPI_GetHSteamPipe");
    NEED(run_frame, "SteamAPI_ManualDispatch_RunFrame");
    NEED(next_callback, "SteamAPI_ManualDispatch_GetNextCallback");
    NEED(free_callback, "SteamAPI_ManualDispatch_FreeLastCallback");
    NEED(get_steam_id, "SteamAPI_ISteamUser_GetSteamID");
    NEED(persona, "SteamAPI_ISteamFriends_GetPersonaName");
    NEED(get_ticket, "SteamAPI_ISteamUser_GetAuthSessionTicket");
    if (!(user = accessor("SteamUser")) || !(friends = accessor("SteamFriends"))) return 1;

    steam_id = get_steam_id(user);
    name = persona(friends);
    printf("steamid: %llu\n", (unsigned long long)steam_id);
    printf("persona: %s\n", name ? name : "(null)");

    handle = get_ticket(user, ticket, sizeof(ticket), &ticket_size, NULL);
    printf("auth ticket: handle %u, %u bytes\n", handle, ticket_size);
    for (i = 0; i < 100 && !got_ticket; i++)
    {
        int32_t pipe = get_pipe();
        CallbackMsg msg;

        run_frame(pipe);
        while (next_callback(pipe, &msg))
        {
            if (msg.id == GET_AUTH_SESSION_TICKET_RESPONSE)
            {
                got_ticket = 1;
                printf("auth ticket: callback, result %d\n", ((int32_t *)msg.param)[1]);
            }
            else printf("callback %d (%d bytes)\n", msg.id, msg.size);
            free_callback(pipe);
        }
        Sleep(100);
    }
    if (!got_ticket) printf("auth ticket: no callback within 10 s\n");

    if (argc > 2 && !strcmp(argv[2], "fault"))
    {
        fflush(stdout);  /* the rows above survive if the fault isn't caught */
#ifdef __i386__
        fault_status = steam_id && name && *name && got_ticket ? 0 : 1;
        AddVectoredExceptionHandler(1, caught);
        store_through_null();
        printf("fault: none raised\n");
        return 1;
#else
        __try
        {
            store_through_null();
            printf("fault: none raised\n");
            return 1;
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            printf("fault: caught\n");
        }
#endif
    }
    return steam_id && name && *name && got_ticket ? 0 : 1;
}
