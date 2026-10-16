/* Plan 3a Task 4: the acceptance script against mn-steam-host's socket, on the Mac, without a VM.
 *   host-client <socket> [--threads N]
 * Exit 2 when the socket can't be reached, 1 when a check fails. Registry writes the host recorded are listed by key
 * and value name only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "../client.h"
#include "script.h"

static const char *path;
static int connect_unix(void) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    snprintf(a.sun_path, sizeof a.sun_path, "%s", path);
    if (s >= 0 && connect(s, (struct sockaddr *)&a, sizeof a) == 0) return s;
    if (s >= 0) close(s);
    return -1;
}
static void show_reg(void *ctx, uint32_t type, const char *key, size_t key_len, const char *name, size_t name_len,
                     const void *data, size_t data_len) {
    (void)ctx; (void)data;
    printf("registry %.*s\\%.*s (type %u, %zu bytes)\n", (int)key_len, key, (int)name_len, name, type, data_len);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: host-client <socket> [--threads N]\n"); return 2; }
    path = argv[1];
    int threads = argc > 3 && !strcmp(argv[2], "--threads") ? atoi(argv[3]) : 1;
    int s = connect_unix();
    if (s < 0) { printf("connect failed\n"); return 2; }
    close(s);
    mn_client_init(connect_unix, show_reg, NULL);
    return mn_script(mn_client_call, threads);
}
