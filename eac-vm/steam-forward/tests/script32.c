/* The acceptance script's rows as a 32-bit game's lsteamclient.dll makes the calls (WoW64 Unix calls): params with
 * 4-byte pointers into memory below 4 GiB, through the guest's WoW64 table to mn-steam-host's WoW64 functions. Steam
 * initialised for app 480, the user's SteamID, an auth ticket, and the persona name, a returned string copied into
 * 32-bit memory by the guest's own steamclient_get_unix_buffer. Prints only redacted rows (no SteamID, persona or
 * ticket bytes). Returns 0 when every check passes. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "unixlib.h"
#include "../client.h"
#include "script.h"

static mn_call_fn call;
static uint8_t *low;
static size_t low_at;
static void *lalloc(size_t n) { void *p = low + low_at; low_at += (n + 15) & ~(size_t)15; memset(p, 0, n); return p; }
static uint32_t lo(const void *p) { return (uint32_t)(uintptr_t)p; }
static uint32_t lstr(const char *s) { char *p = lalloc(strlen(s) + 1); strcpy(p, s); return lo(p); }

#define CALL(fn, p) do { uint32_t st_ = call(unix_##fn, &(p)); \
    if (st_) { printf("FAIL %s: status %#x\n", #fn, st_); return 1; } } while (0)

/* More guest threads than mn-steam-host has low slots (32), each with its connection open while all of them call
 * twice: every call must run (a slot is the call's, not the connection's). */
#define THREADS 40
static struct u_iface th_user;
static pthread_barrier_t th_barrier;
static uint8_t *th_ids[THREADS];
static int th_failed;
static void *th_steamid(void *arg) {
    int i = (int)(intptr_t)arg;
    struct wow64_ISteamUser_SteamUser023_GetSteamID_params sid = {0};
    sid.u_iface = th_user; sid._ret.value = lo(th_ids[i]);
    if (call(unix_ISteamUser_SteamUser023_GetSteamID, &sid)) __atomic_fetch_add(&th_failed, 1, __ATOMIC_RELAXED);
    pthread_barrier_wait(&th_barrier);
    if (call(unix_ISteamUser_SteamUser023_GetSteamID, &sid)) __atomic_fetch_add(&th_failed, 1, __ATOMIC_RELAXED);
    return NULL;
}

int mn_script32(mn_call_fn fn) {
    call = fn;
    low = mmap((void *)0x10000000, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (low != (void *)0x10000000) { printf("FAIL no memory below 4 GiB\n"); return 1; }

    struct wow64_steamclient_init_params init = {0};
    init.g_tmppath.value = lo(lalloc(TEMP_PATH_BUFFER_LENGTH));
    init.steam_app_id.value = lstr("480");
    init.ignore_child_processes_unset = 1;
    CALL(steamclient_init, init);
    printf("init ok\n");
    if (call(unix_steamclient_init_registry, NULL)) { printf("FAIL steamclient_init_registry\n"); return 1; }

    int32_t *rc = lalloc(4);
    struct wow64_steamclient_CreateInterface_params ci = {0};
    ci.name.value = lstr("SteamClient017"); ci.return_code.value = lo(rc);
    CALL(steamclient_CreateInterface, ci);
    if (!ci._ret.handle) { printf("FAIL CreateInterface SteamClient017\n"); return 1; }
    struct u_iface client = ci._ret;

    struct wow64_ISteamClient_SteamClient017_CreateSteamPipe_params pp = {0};
    pp.u_iface = client;
    CALL(ISteamClient_SteamClient017_CreateSteamPipe, pp);
    struct wow64_ISteamClient_SteamClient017_ConnectToGlobalUser_params gu = {0};
    gu.u_iface = client; gu.hSteamPipe = pp._ret;
    CALL(ISteamClient_SteamClient017_ConnectToGlobalUser, gu);
    if (!pp._ret || !gu._ret) { printf("FAIL pipe %d user %d (is Steam running and signed in?)\n", pp._ret, gu._ret); return 1; }

    struct wow64_ISteamClient_SteamClient017_GetISteamUser_params su = {0};
    su.u_iface = client; su.hSteamUser = gu._ret; su.hSteamPipe = pp._ret; su.pchVersion.value = lstr("SteamUser023");
    CALL(ISteamClient_SteamClient017_GetISteamUser, su);
    if (!su._ret.handle) { printf("FAIL GetISteamUser SteamUser023\n"); return 1; }

    uint8_t *id = lalloc(8);
    struct wow64_ISteamUser_SteamUser023_GetSteamID_params sid = {0};
    sid.u_iface = su._ret; sid._ret.value = lo(id);
    CALL(ISteamUser_SteamUser023_GetSteamID, sid);
    uint64_t v; memcpy(&v, id, 8);
    printf(v ? "steamid ok\n" : "steamid FAIL\n");
    if (!v) return 1;

    th_user = su._ret;
    pthread_t th[THREADS];
    pthread_barrier_init(&th_barrier, NULL, THREADS);
    for (int i = 0; i < THREADS; i++) th_ids[i] = lalloc(8);
    for (int i = 0; i < THREADS; i++) pthread_create(&th[i], NULL, th_steamid, (void *)(intptr_t)i);
    for (int i = 0; i < THREADS; i++) pthread_join(th[i], NULL);
    int same = 1;
    for (int i = 0; i < THREADS; i++) same &= !memcmp(th_ids[i], id, 8);
    if (th_failed || !same) { printf("FAIL %d threads: %d calls failed, ids %s\n", THREADS, th_failed, same ? "same" : "differ"); return 1; }
    printf("%d threads at once: steamid ok\n", THREADS);

    uint32_t *cb = lalloc(4);
    struct wow64_ISteamUser_SteamUser023_GetAuthSessionTicket_params t = {0};
    t.u_iface = su._ret; t.pTicket.value = lo(lalloc(1024)); t.cbMaxTicket = 1024; t.pcbTicket.value = lo(cb);
    CALL(ISteamUser_SteamUser023_GetAuthSessionTicket, t);
    if (!t._ret || *cb != 234) { printf("FAIL auth ticket: %u bytes, handle %s\n", *cb, t._ret ? "set" : "0"); return 1; }
    printf("auth ticket 234 bytes\n");
    struct wow64_ISteamUser_SteamUser023_CancelAuthTicket_params c = {0};
    c.u_iface = su._ret; c.hAuthTicket = t._ret;
    CALL(ISteamUser_SteamUser023_CancelAuthTicket, c);

    /* A returned string: the host sends its bytes, the guest keeps a copy, and lsteamclient's 32-bit side has it copied
     * into its own memory (steamclient_get_unix_buffer, answered in the guest). */
    struct wow64_ISteamClient_SteamClient017_GetISteamFriends_params sf = {0};
    sf.u_iface = client; sf.hSteamUser = gu._ret; sf.hSteamPipe = pp._ret; sf.pchVersion.value = lstr("SteamFriends017");
    CALL(ISteamClient_SteamClient017_GetISteamFriends, sf);
    if (!sf._ret.handle) { printf("FAIL GetISteamFriends SteamFriends017\n"); return 1; }
    struct wow64_ISteamFriends_SteamFriends017_GetPersonaName_params pn = {0};
    pn.u_iface = sf._ret;
    CALL(ISteamFriends_SteamFriends017_GetPersonaName, pn);
    if (!pn._ret.ptr || !pn._ret.len) { printf("FAIL persona name: none returned\n"); return 1; }
    struct wow64_steamclient_get_unix_buffer_params gb = {0};
    gb.buf = pn._ret; gb.ptr.value = lo(lalloc(pn._ret.len));
    CALL(steamclient_get_unix_buffer, gb);
    const char *name = (const char *)(uintptr_t)gb.ptr.value;
    printf(gb.ptr.value && strlen(name) + 1 == pn._ret.len ? "persona name ok\n" : "persona name FAIL\n");

    struct wow64_ISteamClient_SteamClient017_ReleaseUser_params ru = {0};
    ru.u_iface = client; ru.hSteamPipe = pp._ret; ru.hUser = gu._ret;
    call(unix_ISteamClient_SteamClient017_ReleaseUser, &ru);
    struct wow64_ISteamClient_SteamClient017_BReleaseSteamPipe_params rp = {0};
    rp.u_iface = client; rp.hSteamPipe = pp._ret;
    call(unix_ISteamClient_SteamClient017_BReleaseSteamPipe, &rp);
    return 0;
}
