#ifndef MN_SCRIPT_H
#define MN_SCRIPT_H
#include <stdint.h>
typedef uint32_t (*mn_call_fn)(uint32_t index, void *params);
int mn_script(mn_call_fn call, int threads);
#endif
