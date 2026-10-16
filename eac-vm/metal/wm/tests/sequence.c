/* The 4b-1 sequence (plan Task 1): a device, two buffers, a blit, a compute dispatch and a shared event that signals a
 * Win32 event, each step printing one line. Params are DXMT's own structs, filled as winemetal's PE thunks fill them. */
#include "sequence.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "winemetal_thunks.h"

enum {
    RETAIN = 0, RELEASE = 1, ARRAY_OBJECT = 2, ARRAY_COUNT = 3, COPY_ALL_DEVICES = 4, DEVICE_NAME = 7,
    STRING_GETCSTRING = 8, NEW_COMMAND_QUEUE = 9, POOL_ALLOC_INIT = 10, COMMAND_BUFFER = 11, COMMIT = 12,
    WAIT_UNTIL_COMPLETED = 13, STATUS = 14, NEW_SHARED_EVENT = 15, ENCODE_SIGNAL_EVENT = 17, NEW_BUFFER = 18,
    NEW_FUNCTION = 26, DESCRIPTION = 28, NEW_COMPUTE_PSO = 29, BLIT_ENCODER = 30, COMPUTE_ENCODER = 31,
    END_ENCODING = 33, BLIT_ENCODE = 36, COMPUTE_ENCODE = 37, SET_WIN32_EVENT = 104, LISTENER_CREATE = 108,
    LISTENER_START = 109, LISTENER_DESTROY = 110, NEW_LIBRARY_WITH_SOURCE = 144,
};

#define WORDS 16384u /* a 64 KiB buffer */
#define FILLED 1024u /* the fill's words */
#define NT_EVENT 0x1234u

static const char kernel_source[] = "#include <metal_stdlib>\n"
                                    "using namespace metal;\n"
                                    "kernel void dbl(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]]) {\n"
                                    "  b[i] *= 2;\n"
                                    "}\n";

struct run {
    const struct mn_wm_env *env;
    obj_handle_t made[32]; /* released in reverse; the pool first in, so last out */
    int nmade;
    obj_handle_t listener;
    pthread_t listener_thread;
    int listening, signalled;
};

static int ok(struct run *r, NTSTATUS_T status, const char *what) {
    if (!status) return 1;
    char line[128];
    snprintf(line, sizeof line, "FAIL %s: status 0x%x", what, status);
    r->env->print(line);
    return 0;
}

static void keep(struct run *r, obj_handle_t h) { r->made[r->nmade++] = h; }

static int string_of(struct run *r, obj_handle_t str, char *buf, uint64_t len) {
    struct unixcall_nsstring_getcstring p = {str, (uint64_t)(uintptr_t)buf, len, WMTUTF8StringEncoding, 0};
    buf[0] = 0;
    return ok(r, r->env->call(STRING_GETCSTRING, &p), "getCString") && p.ret;
}

/* A creation that may hand back an NSError (autoreleased): its description on failure. */
static int no_error(struct run *r, obj_handle_t error, obj_handle_t made, const char *what) {
    if (!error && made) return 1;
    char desc[200] = "(no error)", line[256];
    if (error) {
        struct unixcall_generic_obj_obj_ret d = {error, 0};
        if (ok(r, r->env->call(DESCRIPTION, &d), "description")) string_of(r, d.ret, desc, sizeof desc);
    }
    snprintf(line, sizeof line, "FAIL %s: %s", what, desc);
    r->env->print(line);
    return 0;
}

static void *listen_main(void *p) {
    struct run *r = p;
    struct unixcall_generic_obj_noret a = {r->listener};
    r->env->call(LISTENER_START, &a); /* runs the listener's run loop until LISTENER_DESTROY */
    return NULL;
}

static uint32_t pattern(uint32_t i) { return i * 2654435761u; }

int mn_wm_sequence(const struct mn_wm_env *env, int steps) {
    struct run run = {.env = env}, *r = &run;
    obj_handle_t devices = 0, device = 0, queue = 0, src = 0, dst = 0, cmdbuf = 0, pso = 0, event = 0;
    uint32_t *src_words = NULL, *dst_words = NULL;
    char line[256];
    int step = 0, good = 0;
    if (!steps || steps > 13) steps = 13;

    /* 1. The device, inside the pool every autoreleased object of the run lands in. */
    {
        struct unixcall_generic_obj_ret pool = {0}, all = {0};
        if (!ok(r, env->call(POOL_ALLOC_INIT, &pool), "pool") || !pool.ret) goto out;
        keep(r, pool.ret);
        if (!ok(r, env->call(COPY_ALL_DEVICES, &all), "devices") || !all.ret) goto out;
        keep(r, devices = all.ret);
        struct unixcall_generic_obj_uint64_ret count = {devices, 0};
        if (!ok(r, env->call(ARRAY_COUNT, &count), "count") || !count.ret) goto out;
        struct unixcall_generic_obj_uint64_obj_ret first = {devices, 0, 0};
        if (!ok(r, env->call(ARRAY_OBJECT, &first), "device") || !(device = first.ret)) goto out;
        env->print("device ok");
    }
    if (++step == steps) goto done;

    /* 2. Its name. */
    {
        struct unixcall_generic_obj_obj_ret name = {device, 0};
        char buf[128];
        if (!ok(r, env->call(DEVICE_NAME, &name), "name") || !string_of(r, name.ret, buf, sizeof buf) || !buf[0])
            goto out;
        snprintf(line, sizeof line, "name %s", buf);
        env->print(line);
    }
    if (++step == steps) goto done;

    /* 3. A command queue and a Shared buffer, which Metal allocates. */
    {
        struct unixcall_generic_obj_uint64_obj_ret q = {device, 64, 0}; /* maxCommandBufferCount */
        if (!ok(r, env->call(NEW_COMMAND_QUEUE, &q), "queue") || !q.ret) goto out;
        keep(r, queue = q.ret);
        struct WMTBufferInfo info = {.length = WORDS * 4, .options = WMTResourceStorageModeShared};
        struct unixcall_mtldevice_newbuffer b = {device, {&info}, 0};
        if (!ok(r, env->call(NEW_BUFFER, &b), "buffer") || !b.ret) goto out;
        keep(r, src = b.ret);
        if (!info.memory.ptr || !info.gpu_address) {
            env->print("FAIL buffer: no contents or GPU address");
            goto out;
        }
        src_words = info.memory.ptr;
        env->print("queue ok");
    }
    if (++step == steps) goto done;

    /* 4. A pattern written through the contents pointer. */
    for (uint32_t i = 0; i < WORDS; i++) src_words[i] = pattern(i);
    env->print("buffer ok");
    if (++step == steps) goto done;

    /* 5. The second buffer and a command buffer. */
    {
        struct WMTBufferInfo info = {.length = WORDS * 4, .options = WMTResourceStorageModeShared};
        struct unixcall_mtldevice_newbuffer b = {device, {&info}, 0};
        if (!ok(r, env->call(NEW_BUFFER, &b), "buffer 2") || !b.ret || !info.memory.ptr) goto out;
        keep(r, dst = b.ret);
        dst_words = info.memory.ptr;
        memset(dst_words, 0, WORDS * 4);
        struct unixcall_generic_obj_obj_ret c = {queue, 0};
        if (!ok(r, env->call(COMMAND_BUFFER, &c), "command buffer") || !(cmdbuf = c.ret)) goto out;
        env->print("command buffer ok");
    }
    if (++step == steps) goto done;

    /* 6. Blit: copy src into dst, then fill dst's first FILLED words with 0x5a bytes. */
    {
        struct unixcall_generic_obj_obj_ret e = {cmdbuf, 0};
        if (!ok(r, env->call(BLIT_ENCODER, &e), "blit encoder") || !e.ret) goto out;
        struct wmtcmd_blit_fillbuffer fill = {.type = WMTBlitCommandFillBuffer, .buffer = dst, .offset = 0,
                                              .length = FILLED * 4, .value = 0x5a};
        struct wmtcmd_blit_copy_from_buffer_to_buffer copy = {.type = WMTBlitCommandCopyFromBufferToBuffer,
                                                              .next = {&fill}, .src = src, .dst = dst,
                                                              .copy_length = WORDS * 4};
        struct unixcall_generic_obj_cmd_noret enc = {e.ret, {&copy}};
        struct unixcall_generic_obj_noret end = {e.ret};
        if (!ok(r, env->call(BLIT_ENCODE, &enc), "blit") || !ok(r, env->call(END_ENCODING, &end), "blit end"))
            goto out;
        env->print("blit ok");
    }
    if (++step == steps) goto done;

    /* 7. A kernel compiled from source that doubles every word, dispatched over dst. */
    {
        struct unixcall_mtldevice_newlibrarywithsource lib = {device, {kernel_source}, sizeof kernel_source - 1, 0, 0};
        if (!ok(r, env->call(NEW_LIBRARY_WITH_SOURCE, &lib), "library")) goto out;
        if (lib.ret_library) keep(r, lib.ret_library);
        if (!no_error(r, lib.ret_error, lib.ret_library, "library")) goto out;
        struct unixcall_generic_obj_uint64_obj_ret fn = {lib.ret_library, (uint64_t)(uintptr_t) "dbl", 0};
        if (!ok(r, env->call(NEW_FUNCTION, &fn), "function") || !fn.ret) goto out;
        keep(r, fn.ret);
        struct WMTComputePipelineInfo pinfo = {.compute_function = fn.ret};
        struct unixcall_mtldevice_newcomputepso p = {device, {&pinfo}, 0, 0};
        if (!ok(r, env->call(NEW_COMPUTE_PSO, &p), "pipeline")) goto out;
        if (p.ret_pso) keep(r, pso = p.ret_pso);
        if (!no_error(r, p.ret_error, p.ret_pso, "pipeline")) goto out;
        struct unixcall_generic_obj_uint64_obj_ret e = {cmdbuf, 0, 0};
        if (!ok(r, env->call(COMPUTE_ENCODER, &e), "compute encoder") || !e.ret) goto out;
        struct wmtcmd_compute_dispatch dispatch = {.type = WMTComputeCommandDispatch, .size = {WORDS / 64, 1, 1}};
        struct wmtcmd_compute_setbuffer buf = {.type = WMTComputeCommandSetBuffer, .next = {&dispatch}, .buffer = dst,
                                               .index = 0};
        struct wmtcmd_compute_setpso set = {.type = WMTComputeCommandSetPSO, .next = {&buf}, .pso = pso,
                                            .threadgroup_size = {64, 1, 1}};
        struct unixcall_generic_obj_cmd_noret enc = {e.ret, {&set}};
        struct unixcall_generic_obj_noret end = {e.ret};
        if (!ok(r, env->call(COMPUTE_ENCODE, &enc), "compute") || !ok(r, env->call(END_ENCODING, &end), "compute end"))
            goto out;
        env->print("pipeline ok");
    }
    if (++step == steps) goto done;

    /* 8. A shared event. */
    {
        struct unixcall_generic_obj_obj_ret e = {device, 0};
        if (!ok(r, env->call(NEW_SHARED_EVENT, &e), "shared event") || !e.ret) goto out;
        keep(r, event = e.ret);
        env->print("shared event ok");
    }
    if (++step == steps) goto done;

    /* 9. A listener on its own thread, and Win32 event NT_EVENT set when the shared event reaches 1. */
    {
        struct unixcall_generic_obj_ret l = {0};
        if (!ok(r, env->call(LISTENER_CREATE, &l), "listener") || !(r->listener = l.ret)) goto out;
        if (pthread_create(&r->listener_thread, NULL, listen_main, r)) goto out;
        r->listening = 1;
        struct unixcall_mtlsharedevent_setevent s = {event, NT_EVENT, r->listener, 1};
        if (!ok(r, env->call(SET_WIN32_EVENT, &s), "win32 event")) goto out;
        env->print("listener ok");
    }
    if (++step == steps) goto done;

    /* 10. The command buffer signals the event at 1. */
    {
        struct unixcall_generic_obj_obj_uint64_noret s = {cmdbuf, event, 1};
        if (!ok(r, env->call(ENCODE_SIGNAL_EVENT, &s), "signal")) goto out;
        env->print("signal encoded");
    }
    if (++step == steps) goto done;

    /* 11. Commit and wait. */
    {
        struct unixcall_generic_obj_noret c = {cmdbuf};
        if (!ok(r, env->call(COMMIT, &c), "commit") || !ok(r, env->call(WAIT_UNTIL_COMPLETED, &c), "wait")) goto out;
        env->print("committed");
    }
    if (++step == steps) goto done;

    /* 12. dst holds the doubled copy, the command buffer completed, and the Win32 event was set. */
    {
        struct unixcall_generic_obj_uint64_ret s = {cmdbuf, 0};
        if (!ok(r, env->call(STATUS, &s), "status")) goto out;
        uint64_t sum = 0;
        for (uint32_t i = 0; i < WORDS; i++) {
            uint32_t want = (i < FILLED ? 0x5a5a5a5au : pattern(i)) * 2u;
            if (dst_words[i] != want) {
                snprintf(line, sizeof line, "FAIL compute: word %u is 0x%x, not 0x%x", i, dst_words[i], want);
                env->print(line);
                goto out;
            }
            sum += want;
        }
        snprintf(line, sizeof line, "compute ok sum=%llu status=%llu", (unsigned long long)sum,
                 (unsigned long long)s.ret);
        env->print(line);
        if (s.ret != 4 /* MTLCommandBufferStatusCompleted */) goto out;
        if (!env->signalled(NT_EVENT, 5000)) {
            env->print("FAIL event 0x1234 not signalled");
            goto out;
        }
        r->signalled = 1;
        env->print("event 0x1234 signalled");
    }
    ++step;

done:
    good = 1;
out:
    /* 13. Release everything, the listener first and the pool (made[0]) last. */
    if (r->listening) {
        /* ponytail: destroy only stops a run loop already running; on a run cut before step 12 give it time. */
        if (!r->signalled) usleep(100000);
        struct unixcall_generic_obj_noret d = {r->listener};
        env->call(LISTENER_DESTROY, &d);
        pthread_join(r->listener_thread, NULL);
    }
    for (int i = r->nmade - 1; i >= 0; i--) {
        obj_handle_t h = r->made[i];
        good &= ok(r, env->call(RELEASE, &h), "release");
    }
    if (good && step >= 12) env->print("released ok");
    return good ? 0 : -1;
}
