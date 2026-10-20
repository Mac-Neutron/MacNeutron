#ifndef MN_SCRIPT_H
#define MN_SCRIPT_H
#include <stdint.h>
typedef uint32_t (*mn_call_fn)(uint32_t index, void *params);
int mn_script(mn_call_fn call, int threads);
/* mn_script, then `bench` timed GetSteamID calls (prints the median round trip), then GetSteamID calls for `loop`
 * seconds, stopping at the first error and printing how long that call took to fail. */
int mn_script_ex(mn_call_fn call, int threads, int bench, int loop);
/* The script's rows as a 32-bit (WoW64) caller makes them, plus the persona name (tests/script32.c). */
int mn_script32(mn_call_fn call);
#endif
