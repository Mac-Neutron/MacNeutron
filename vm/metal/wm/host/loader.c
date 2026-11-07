#include "loader.h"
#include <dlfcn.h>
#include <stdio.h>

const wm_unix_fn *mn_wm_funcs;
static void (*set_hook)(void (*)(uint64_t));

int mn_wm_load(const char *dir) {
    char path[1024];
    /* The stand-in first, for its hook setter; winemetal's @rpath/ntdll.so (@loader_path/) is then this image. */
    snprintf(path, sizeof path, "%s/ntdll.so", dir);
    void *nt = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!nt) goto fail;
    snprintf(path, sizeof path, "%s/winemetal.so", dir);
    void *wm = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!wm) goto fail;
    set_hook = (void (*)(void (*)(uint64_t)))dlsym(nt, "mn_ntdll_set_hook");
    mn_wm_funcs = (const wm_unix_fn *)dlsym(wm, "__wine_unix_call_funcs");
    if (set_hook && mn_wm_funcs) return 0;
    fprintf(stderr, "mn_wm_load: %s lacks mn_ntdll_set_hook or __wine_unix_call_funcs\n", dir);
    return -1;
fail:
    fprintf(stderr, "mn_wm_load: %s\n", dlerror());
    return -1;
}

void mn_wm_set_event_hook(void (*hook)(uint64_t nt_event)) {
    if (set_hook) set_hook(hook);
}
