/* The Mac bridge's acceptance check (bridge/probe.sh) as forwarded Unix calls: Steam initialised for app 480, the
 * user's SteamID, an auth ticket; then the same ticket call from `threads` threads at once, each checking its own
 * reply. Prints only redacted rows (no SteamID, persona or ticket bytes). Returns 0 when every check passes. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "unixlib.h"
#include "../client.h"
#include "script.h"

static mn_call_fn call;
static struct u_iface user_iface;
static int failed;

#define CALL(fn, p) do { uint32_t st_ = call(unix_##fn, &(p)); \
    if (st_) { printf("FAIL %s: status %#x\n", #fn, st_); return 1; } } while (0)

static int ticket(uint32_t *handle) {
    uint8_t buf[1024]; uint32_t cb = 0;
    struct ISteamUser_SteamUser023_GetAuthSessionTicket_params t;
    memset(&t, 0, sizeof t);
    t.u_iface = user_iface; t.pTicket = buf; t.cbMaxTicket = sizeof buf; t.pcbTicket = &cb; t.pSteamNetworkingIdentity = NULL;
    CALL(ISteamUser_SteamUser023_GetAuthSessionTicket, t);
    *handle = t._ret;
    return t._ret && cb == 234 ? 0 : (printf("FAIL auth ticket: %u bytes, handle %s\n", cb, t._ret ? "set" : "0"), 1);
}
static int cancel(uint32_t handle) {
    struct ISteamUser_SteamUser023_CancelAuthTicket_params c;
    memset(&c, 0, sizeof c); c.u_iface = user_iface; c.hAuthTicket = handle;
    CALL(ISteamUser_SteamUser023_CancelAuthTicket, c);
    return 0;
}

static uint32_t handles[64];
static void *worker(void *arg) {
    int i = (int)(intptr_t)arg;
    if (ticket(&handles[i]) || cancel(handles[i])) __atomic_fetch_add(&failed, 1, __ATOMIC_RELAXED);
    return NULL;
}

int mn_script(mn_call_fn fn, int threads) {
    call = fn;
    char tmp[TEMP_PATH_BUFFER_LENGTH];
    struct steamclient_init_params init;
    memset(&init, 0, sizeof init);
    init.g_tmppath = tmp; init.steam_app_id = "480"; init.ignore_child_processes_unset = 1;
    CALL(steamclient_init, init);
    printf("init ok\n");
    if (call(unix_steamclient_init_registry, NULL)) { printf("FAIL steamclient_init_registry\n"); return 1; }

    int rc = -1;
    struct steamclient_CreateInterface_params ci;
    memset(&ci, 0, sizeof ci); ci.name = "SteamClient017"; ci.return_code = &rc;
    CALL(steamclient_CreateInterface, ci);
    if (!ci._ret.handle) { printf("FAIL CreateInterface SteamClient017\n"); return 1; }
    struct u_iface client = ci._ret;

    struct ISteamClient_SteamClient017_CreateSteamPipe_params pp;
    memset(&pp, 0, sizeof pp); pp.u_iface = client;
    CALL(ISteamClient_SteamClient017_CreateSteamPipe, pp);
    struct ISteamClient_SteamClient017_ConnectToGlobalUser_params gu;
    memset(&gu, 0, sizeof gu); gu.u_iface = client; gu.hSteamPipe = pp._ret;
    CALL(ISteamClient_SteamClient017_ConnectToGlobalUser, gu);
    if (!pp._ret || !gu._ret) { printf("FAIL pipe %d user %d (is Steam running and signed in?)\n", pp._ret, gu._ret); return 1; }

    struct ISteamClient_SteamClient017_GetISteamUser_params su;
    memset(&su, 0, sizeof su); su.u_iface = client; su.hSteamUser = gu._ret; su.hSteamPipe = pp._ret; su.pchVersion = "SteamUser023";
    CALL(ISteamClient_SteamClient017_GetISteamUser, su);
    if (!su._ret.handle) { printf("FAIL GetISteamUser SteamUser023\n"); return 1; }
    user_iface = su._ret;

    CSteamID id; memset(&id, 0, sizeof id);
    struct ISteamUser_SteamUser023_GetSteamID_params sid;
    memset(&sid, 0, sizeof sid); sid.u_iface = user_iface; sid._ret = &id;
    CALL(ISteamUser_SteamUser023_GetSteamID, sid);
    uint64_t v; memcpy(&v, &id, 8);
    printf(v ? "steamid ok\n" : "steamid FAIL\n");
    if (!v) return 1;

    uint32_t h;
    if (ticket(&h)) return 1;
    printf("auth ticket 234 bytes\n");
    cancel(h);

    if (threads > 1) {
        pthread_t t[64];
        if (threads > 64) threads = 64;
        for (int i = 0; i < threads; i++) pthread_create(&t[i], NULL, worker, (void *)(intptr_t)i);
        for (int i = 0; i < threads; i++) pthread_join(t[i], NULL);
        int distinct = 1;
        for (int i = 0; i < threads; i++) for (int j = i + 1; j < threads; j++) if (handles[i] == handles[j]) distinct = 0;
        if (failed || !distinct) { printf("FAIL %d threads: %d failed, tickets %s\n", threads, failed, distinct ? "distinct" : "repeated"); return 1; }
        printf("%d threads: %d auth tickets of 234 bytes\n", threads, threads);
    }

    struct ISteamClient_SteamClient017_ReleaseUser_params ru;
    memset(&ru, 0, sizeof ru); ru.u_iface = client; ru.hSteamPipe = pp._ret; ru.hUser = gu._ret;
    call(unix_ISteamClient_SteamClient017_ReleaseUser, &ru);
    struct ISteamClient_SteamClient017_BReleaseSteamPipe_params rp;
    memset(&rp, 0, sizeof rp); rp.u_iface = client; rp.hSteamPipe = pp._ret;
    call(unix_ISteamClient_SteamClient017_BReleaseSteamPipe, &rp);
    return 0;
}
