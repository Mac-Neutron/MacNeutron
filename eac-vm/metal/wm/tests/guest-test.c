/* Milestone 4b-1: the 4b-1 sequence in the guest, through the guest winemetal.so as Wine would load it (dlopen,
 * RTLD_NOW), with the stub ntdll.so on LD_LIBRARY_PATH. Prints the sequence's lines, then the host's stats.
 *   metal-wm-test [--steps N] */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../calls.h"
#include "sequence.h"

typedef NTSTATUS_T (*entry_t)(void *params);
static const entry_t *funcs;

static NTSTATUS_T call(unsigned slot, void *params) { return funcs[slot](params); }
static void print(const char *line) {
    printf("%s\n", line);
    fflush(stdout);
}

int main(int argc, char **argv) {
    int steps = argc == 3 && !strcmp(argv[1], "--steps") ? atoi(argv[2]) : 0;
    void *wm = dlopen("winemetal.so", RTLD_NOW);
    void *nt = dlopen("ntdll.so", RTLD_NOW);
    if (!wm || !nt) {
        fprintf(stderr, "metal-wm-test: %s\n", dlerror());
        return 2;
    }
    funcs = dlsym(wm, "__wine_unix_call_funcs");
    int (*stats)(struct mn_wm_stats *) = (int (*)(struct mn_wm_stats *))dlsym(wm, "mn_wm_stats");
    struct mn_wm_env env = {call, print, (int (*)(uint64_t, int))dlsym(nt, "mn_stub_signalled")};
    if (!funcs || !stats || !env.signalled) {
        fprintf(stderr, "metal-wm-test: missing symbols\n");
        return 2;
    }
    int r = mn_wm_sequence(&env, steps);
    struct mn_wm_stats st;
    if (stats(&st)) {
        fprintf(stderr, "metal-wm-test: no stats\n");
        return 1;
    }
    printf("stats failures=%llu live=%llu deferred=%llu\n", (unsigned long long)st.failures,
           (unsigned long long)st.live, (unsigned long long)st.deferred);
    return r ? 1 : 0;
}
