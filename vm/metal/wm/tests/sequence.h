/* Milestone 4b-1's call sequence, written once: host-test runs it on the Mac against winemetal's own table, and the
 * guest gate runs it through the guest winemetal.so. Both must print the same lines. */
#ifndef MN_WM_SEQUENCE_H
#define MN_WM_SEQUENCE_H
#include <stdint.h>

typedef unsigned int NTSTATUS_T;

struct mn_wm_env {
    NTSTATUS_T (*call)(unsigned slot, void *params);    /* the real table on the Mac, the forwarder in the guest */
    void (*print)(const char *line);
    int (*signalled)(uint64_t nt_event, int timeout_ms); /* 1 once NtSetEvent(nt_event) happened */
};

/* Runs steps 1..steps (0 = all 13), then releases what it made. 0 when every step's result matched. */
int mn_wm_sequence(const struct mn_wm_env *env, int steps);

#endif
