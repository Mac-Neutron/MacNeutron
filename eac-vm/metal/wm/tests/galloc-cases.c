/* The guest allocator's cases (plan Task 5), through the forwarder: run by memory-test on the Mac (loopback) and by
 * metal-wm-test in the guest. Buffers over blob memory start on a 16 KiB page and span whole pages, and a freed
 * buffer's memory comes back only after the host says the Metal buffer is gone (the stats call sweeps releases). */
#include "galloc-cases.h"
#include <stdint.h>
#include <stdio.h>
#include "winemetal_thunks.h"

#define PAGE 16384u

static obj_handle_t new_buffer(const struct mn_galloc_env *env, obj_handle_t device, uint64_t len, void **ptr) {
    struct WMTBufferInfo info = {.length = len, .options = WMTResourceStorageModeShared};
    struct unixcall_mtldevice_newbuffer b = {device, {&info}, 0};
    if (env->call(18, &b) || !b.ret) return 0;
    *ptr = info.memory.ptr;
    return b.ret;
}

int mn_galloc_cases(const struct mn_galloc_env *env) {
    char line[160];
    int bad = 0;
#define EXPECT(c, what)                                                                                                \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            snprintf(line, sizeof line, "FAIL galloc: %s", what);                                                     \
            env->print(line);                                                                                          \
            bad = 1;                                                                                                   \
        }                                                                                                              \
    } while (0)
    struct unixcall_generic_obj_ret all = {0};
    env->call(4, &all);
    struct unixcall_generic_obj_uint64_obj_ret first = {all.ret, 0, 0};
    env->call(2, &first);
    obj_handle_t device = first.ret;
    void *p1, *p2, *p3, *p4;
    obj_handle_t b1 = new_buffer(env, device, PAGE, &p1), b2 = new_buffer(env, device, 20000, &p2);
    EXPECT(b1 && b2 && p1 && p2, "buffers");
    EXPECT(!((uintptr_t)p1 % PAGE) && !((uintptr_t)p2 % PAGE), "16 KiB alignment");
    /* p1 covers one page, p2 two (20,000 bytes rounded up): they don't overlap. */
    EXPECT((char *)p2 >= (char *)p1 + PAGE || (char *)p1 >= (char *)p2 + 2 * PAGE, "whole pages");
    env->call(1, &b1);
    obj_handle_t b3 = new_buffer(env, device, PAGE, &p3);
    EXPECT(b3 && p3 != p1, "reused before the host freed it");
    struct mn_wm_stats st;
    env->stats(&st); /* sweeps: the host's buffer goes, and with it the region */
    obj_handle_t b4 = new_buffer(env, device, PAGE, &p4);
    EXPECT(b4 && p4 == p1, "not reused after the host freed it");
    env->call(1, &b2);
    env->call(1, &b3);
    env->call(1, &b4);
    env->call(1, &all.ret);
    if (!bad) env->print("galloc ok");
    return bad;
}
