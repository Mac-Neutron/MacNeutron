/* The VM host loads MacNeutron's own winemetal.so (milestone 4b-1) from a directory where it sits beside our
 * stand-ins for ntdll.so and winemac.so (stand-ins.c). */
#ifndef MN_WM_LOADER_H
#define MN_WM_LOADER_H
#include <stdint.h>

typedef unsigned int NTSTATUS_T;
typedef NTSTATUS_T (*wm_unix_fn)(void *params);

/* dlopens dir/winemetal.so; 0, or -1 with the reason on stderr. */
int mn_wm_load(const char *dir);
/* Its __wine_unix_call_funcs[153], once loaded. */
extern const wm_unix_fn *mn_wm_funcs;
/* What the stand-in NtSetEvent calls, with the Win32 event handle winemetal was given. */
void mn_wm_set_event_hook(void (*hook)(uint64_t nt_event));

#endif
