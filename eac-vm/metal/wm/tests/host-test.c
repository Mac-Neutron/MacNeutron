/* Milestone 4b-1, Task 1: the 4b-1 sequence run on the Mac directly against MacNeutron's winemetal.so, loaded beside
 * our stand-ins for its Wine libraries. Prints the sequence's lines on stdout (the guest gate's reference); checks go
 * to stderr. Usage: host-test <metal-host dir>. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "../host/loader.h"
#include "sequence.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static uint64_t events[64];
static int nevents;

static void on_event(uint64_t nt_event) {
    pthread_mutex_lock(&lock);
    if (nevents < 64) events[nevents++] = nt_event;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
}

static int signalled(uint64_t nt_event, int timeout_ms) {
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += timeout_ms / 1000;
    end.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (end.tv_nsec >= 1000000000) end.tv_sec++, end.tv_nsec -= 1000000000;
    pthread_mutex_lock(&lock);
    int found = 0;
    for (;;) {
        for (int i = 0; i < nevents; i++) found |= events[i] == nt_event;
        if (found || pthread_cond_timedwait(&cond, &lock, &end)) break;
    }
    pthread_mutex_unlock(&lock);
    return found;
}

static NTSTATUS_T call(unsigned slot, void *params) { return mn_wm_funcs[slot](params); }

static char lines[64][256];
static int nlines;

static void print(const char *line) {
    if (nlines < 64) snprintf(lines[nlines++], sizeof lines[0], "%s", line);
    printf("%s\n", line);
    fflush(stdout);
}

/* The lines the sequence must print, in this order (each a prefix of its line). */
static const char *required[] = {"device ok", "name ", "blit ok", "compute ok sum=", "event 0x1234 signalled",
                                 "released ok"};

int main(int argc, char **argv) {
    if (argc != 2 || mn_wm_load(argv[1])) {
        fprintf(stderr, "host-test: FAIL loading winemetal.so from %s\n", argc == 2 ? argv[1] : "(no dir)");
        return 1;
    }
    mn_wm_set_event_hook(on_event);
    struct mn_wm_env env = {call, print, signalled};
    int r = mn_wm_sequence(&env, 0), next = 0;
    for (int i = 0; i < nlines && next < (int)(sizeof required / sizeof *required); i++)
        if (!strncmp(lines[i], required[next], strlen(required[next])) &&
            (next != 1 || strlen(lines[i]) > strlen("name ")))
            next++;
    int ok = r == 0 && next == (int)(sizeof required / sizeof *required);
    if (!ok) fprintf(stderr, "host-test: FAIL (sequence %d, %d of %zu required lines)\n", r, next,
                     sizeof required / sizeof *required);
    else fprintf(stderr, "host-test: ok\n");
    return !ok;
}
