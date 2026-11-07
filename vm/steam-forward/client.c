/* The forwarding client: see client.h. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "client.h"
#include "unixlib.h"

static mn_connect_fn connect_fn;
static mn_reg_fn reg_apply;
static void *reg_ctx;
static __thread int conn = -1;

void mn_client_init(mn_connect_fn connect, mn_reg_fn apply, void *ctx) {
    connect_fn = connect; reg_apply = apply; reg_ctx = ctx;
}

int mn_read_full(int fd, void *buf, size_t n) {
    for (size_t got = 0; got < n;) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r > 0) got += (size_t)r;
        else if (r < 0 && errno == EINTR) continue;
        else return -1;
    }
    return 0;
}
int mn_write_full(int fd, const void *buf, size_t n) {
    for (size_t put = 0; put < n;) {
        ssize_t r = write(fd, (const char *)buf + put, n - put);
        if (r > 0) put += (size_t)r;
        else if (r < 0 && errno == EINTR) continue;
        else return -1;
    }
    return 0;
}

static void drop(void) { if (conn >= 0) close(conn); conn = -1; }

static int ensure_connected(void) {
    if (conn >= 0) return 0;
    if (!connect_fn || (conn = connect_fn()) < 0) { conn = -1; return -1; }
    /* The handshake is bounded (2 s): a transport can accept the connection with nobody behind it (libkrun's vsock does
     * when mn-steam-host is gone), and a forwarded call must fail, not hang. Calls themselves are not bounded. */
    struct timeval two = { 2, 0 }, none = { 0, 0 };
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &two, sizeof two);
    uint32_t mine = mn_handshake_mine(), theirs = 0;
    if (mn_write_full(conn, &mine, 4) || mn_read_full(conn, &theirs, 4) || theirs != mine) {
        if (theirs && theirs != mine) fprintf(stderr, "mn-steam: host table %08x differs from ours %08x\n", theirs, mine);
        else fprintf(stderr, "mn-steam: no answer from the host\n");
        drop();
        return -1;
    }
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof none);
    return 0;
}

uint32_t mn_tables_checksum(void) {
    return mn_table_checksum(mn_calls, mn_ncalls) ^ (mn_table_checksum(mn_calls_wow64, mn_ncalls) * 16777619u);
}

/* What this guest sends in the handshake: an i686 build (old WoW64's 32-bit Unix side) makes WoW64 calls only. */
uint32_t mn_handshake_mine(void) {
#ifdef __i386__
    return mn_handshake_value(mn_tables_checksum(), mn_table_checksum(mn_calls_wow64, mn_ncalls), 1);
#else
    return mn_tables_checksum();
#endif
}

/* With no host to reach, answer as lsteamclient does when Steam isn't running: init and the registry setup succeed and
 * CreateInterface finds no interface, so SteamAPI_Init returns false. lsteamclient's PE side asserts on any failed
 * call, which would stop the game at an assertion dialog instead. */
static uint32_t no_steam(const struct mn_call_desc *d, void *params) {
    if (!strcmp(d->name, "steamclient_init") || !strcmp(d->name, "steamclient_init_registry")) return 0;
    if (!strcmp(d->name, "steamclient_CreateInterface")) {
        /* _ret comes first in the WoW64 params too */
        ((struct steamclient_CreateInterface_params *)params)->_ret.handle = 0;
        return 0;
    }
    return MN_STATUS_DISCONNECTED; /* ponytail: a host lost mid-game still asserts, as a Steam crash would */
}

/* One call: `wire` is its index, with MN_WOW64 for a WoW64 call. */
static uint32_t call(const struct mn_call_desc *d, uint32_t wire, void *params) {
    uint32_t index = wire & ~MN_WOW64;
    struct mn_buf req = {0};
    int r = mn_pack_request(d, wire, params, &req);
    if (r == MN_E_UNSUPPORTED) {
        static __thread uint32_t last; /* log each unsupported call once per run of repeats */
        if (last != index + 1) fprintf(stderr, "mn-steam: %s is not carried to the host\n", d->name);
        last = index + 1;
        return MN_STATUS_NOT_IMPLEMENTED;
    }
    if (r) { fprintf(stderr, "mn-steam: %s: cannot pack (%d)\n", d->name, r); return MN_STATUS_INVALID_PARAMETER; }
    uint32_t status = MN_STATUS_DISCONNECTED;
    struct mn_rep_hdr h;
    uint8_t *msg = NULL;
    static __thread int lost; /* say a lost host once per run of failed calls (a 32-bit game's, with no 32-bit host) */
    if (ensure_connected() || mn_write_full(conn, req.data, req.len) || mn_read_full(conn, &h, sizeof h) ||
        h.magic != MN_MAGIC || h.len > MN_MAX_MESSAGE || !(msg = malloc(sizeof h + h.len))) {
        if (!lost++) fprintf(stderr, "mn-steam: %s: host unreachable\n", d->name);
        drop();
    } else if (memcpy(msg, &h, sizeof h), mn_read_full(conn, msg + sizeof h, h.len)) {
        if (!lost++) fprintf(stderr, "mn-steam: %s: host closed the connection\n", d->name);
        drop();
    } else if ((r = mn_unpack_reply(d, msg, sizeof h + h.len, params, &status, reg_apply, reg_ctx))) {
        fprintf(stderr, "mn-steam: %s: bad reply (%d)\n", d->name, r);
        status = MN_STATUS_INVALID_PARAMETER;
        drop();
    } else {
        lost = 0;
    }
    free(msg);
    mn_buf_free(&req);
    return status == MN_STATUS_DISCONNECTED ? no_steam(d, params) : status;
}

uint32_t mn_client_call(uint32_t index, void *params) {
    return index < mn_ncalls ? call(&mn_calls[index], index, params) : MN_STATUS_INVALID_PARAMETER;
}
uint32_t mn_client_call_wow64(uint32_t index, void *params) {
    return index < mn_ncalls ? call(&mn_calls_wow64[index], index | MN_WOW64, params) : MN_STATUS_INVALID_PARAMETER;
}

/* lsteamclient's 32-bit side copies a returned string into memory it can reach through this call, as Proton's Unix
 * side does: into the caller's buffer the first time, then the same buffer again for the same string (the caller frees
 * its new one). The string is this process's kept copy (marshal.c), so the call is answered here, never sent.
 * ponytail: a linear list, as marshal.c's kept copies. */
struct seen { struct seen *next; uint64_t src; uint32_t dst; };
/* A W32_PTR field's 32-bit value, read and written: a struct holding it in a 64-bit build, the pointer itself in an
 * i686 one. */
#ifdef __i386__
#define W32_GET(f) ((uint32_t)(uintptr_t)(f))
#define W32_SET(f, v) ((f) = (void *)(uintptr_t)(v))
#else
#define W32_GET(f) ((f).value)
#define W32_SET(f, v) ((f).value = (v))
#endif
static struct seen *seen_list;
static pthread_mutex_t seen_lock = PTHREAD_MUTEX_INITIALIZER;
NTSTATUS mn_wow64_get_unix_buffer(void *args) {
    struct wow64_steamclient_get_unix_buffer_params *p = args;
    if (!W32_GET(p->ptr) || !p->buf.ptr) return MN_STATUS_INVALID_PARAMETER;
    pthread_mutex_lock(&seen_lock);
    struct seen *s = seen_list;
    while (s && s->src != p->buf.ptr) s = s->next;
    if (s) W32_SET(p->ptr, s->dst);
    else {
        memcpy((void *)(uintptr_t)W32_GET(p->ptr), (const void *)(uintptr_t)p->buf.ptr, p->buf.len);
        if ((s = malloc(sizeof *s))) { s->src = p->buf.ptr; s->dst = W32_GET(p->ptr); s->next = seen_list; seen_list = s; }
    }
    pthread_mutex_unlock(&seen_lock);
    return 0;
}
