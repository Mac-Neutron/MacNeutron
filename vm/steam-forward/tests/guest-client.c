/* Plan 3a Task 5: the acceptance script inside the guest, through the generated forwarding table and vsock, as Wine
 * will call it; with --wow64, as a 32-bit game's calls come (the WoW64 table, tests/script32.c).
 *   guest-client [--threads N] [--bench N] [--loop SECONDS] | --wow64
 * Exit 2 when the host can't be reached, 1 when a check fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "unixlib.h"
#include "../client.h"
#include "../vsock.h"
#include "script.h"

extern const unixlib_entry_t __wine_unix_call_funcs[], __wine_unix_call_wow64_funcs[];
static uint32_t through_table(uint32_t index, void *params) { return __wine_unix_call_funcs[index](params); }
static uint32_t through_wow64(uint32_t index, void *params) { return __wine_unix_call_wow64_funcs[index](params); }
static void show_reg(void *ctx, uint32_t type, const char *key, size_t key_len, const char *name, size_t name_len,
                     const void *data, size_t data_len) {
    (void)ctx; (void)data;
    printf("registry %.*s\\%.*s (type %u, %zu bytes)\n", (int)key_len, key, (int)name_len, name, type, data_len);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* the gate reads it from the share as it runs */
    int threads = 1, bench = 0, loop = 0, wow64 = argc > 1 && !strcmp(argv[1], "--wow64");
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--threads")) threads = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--bench")) bench = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--loop")) loop = atoi(argv[i + 1]);
    }
    int s = mn_vsock_connect();
    if (s < 0) { printf("connect failed\n"); return 2; }
    close(s);
    mn_client_init(mn_vsock_connect, show_reg, NULL);
    return wow64 ? mn_script32(through_wow64) : mn_script_ex(through_table, threads, bench, loop);
}
