/* The guest's connection to mn-steam-host: vsock port 5101 on the host, which libkrun maps to its Unix socket.
 * The connect is bounded (2 s): when mn-steam-host is gone, libkrun leaves a guest connect pending instead of
 * refusing it, and a forwarded call must fail, not hang. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/vm_sockets.h>
#include "vsock.h"

int mn_vsock_connect(void) {
    int s = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (s < 0) return -1;
    struct sockaddr_vm a = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = MN_STEAM_VSOCK_PORT };
    int r = connect(s, (struct sockaddr *)&a, sizeof a);
    if (r && errno == EINPROGRESS) {
        struct pollfd p = { .fd = s, .events = POLLOUT };
        int err = 0; socklen_t len = sizeof err;
        r = poll(&p, 1, 2000) == 1 && !getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) && !err ? 0 : -1;
    }
    if (r) { close(s); return -1; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
    return s;
}
