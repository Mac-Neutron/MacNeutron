/* The forwarding client: see client.h. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include "client.h"

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
    uint32_t mine = mn_table_checksum(mn_calls, mn_ncalls), theirs = 0;
    if (mn_write_full(conn, &mine, 4) || mn_read_full(conn, &theirs, 4) || theirs != mine) {
        if (theirs && theirs != mine) fprintf(stderr, "mn-steam: host table %08x differs from ours %08x\n", theirs, mine);
        else fprintf(stderr, "mn-steam: no answer from the host\n");
        drop();
        return -1;
    }
    setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof none);
    return 0;
}

uint32_t mn_client_call(uint32_t index, void *params) {
    if (index >= mn_ncalls) return MN_STATUS_INVALID_PARAMETER;
    const struct mn_call_desc *d = &mn_calls[index];
    struct mn_buf req = {0};
    int r = mn_pack_request(d, index, params, &req);
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
    if (ensure_connected() || mn_write_full(conn, req.data, req.len) || mn_read_full(conn, &h, sizeof h) ||
        h.magic != MN_MAGIC || h.len > MN_MAX_MESSAGE || !(msg = malloc(sizeof h + h.len))) {
        fprintf(stderr, "mn-steam: %s: host unreachable\n", d->name);
        drop();
    } else if (memcpy(msg, &h, sizeof h), mn_read_full(conn, msg + sizeof h, h.len)) {
        fprintf(stderr, "mn-steam: %s: host closed the connection\n", d->name);
        drop();
    } else if ((r = mn_unpack_reply(d, msg, sizeof h + h.len, params, &status, reg_apply, reg_ctx))) {
        fprintf(stderr, "mn-steam: %s: bad reply (%d)\n", d->name, r);
        status = MN_STATUS_INVALID_PARAMETER;
        drop();
    }
    free(msg);
    mn_buf_free(&req);
    return status;
}
