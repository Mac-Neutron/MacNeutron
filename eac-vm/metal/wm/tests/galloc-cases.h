#ifndef MN_WM_GALLOC_CASES_H
#define MN_WM_GALLOC_CASES_H
#include "../calls.h"

typedef unsigned int NTSTATUS_T;
struct mn_galloc_env {
    NTSTATUS_T (*call)(unsigned slot, void *params);
    int (*stats)(struct mn_wm_stats *st);
    void (*print)(const char *line);
};
int mn_galloc_cases(const struct mn_galloc_env *env); /* 0 when every case held */

#endif
