/* Milestone 4b-1: the 4b-1 sequence in the guest, through the guest winemetal.so as Wine would load it (dlopen,
 * RTLD_NOW), with the stub ntdll.so on LD_LIBRARY_PATH. Prints the sequence's lines, then the host's stats.
 *   metal-wm-test [--steps N]   the sequence, then the stats line
 *   metal-wm-test galloc        the guest allocator's cases (galloc-cases.c)
 *   metal-wm-test refused       each slot the guest refuses answers its status, twice (it logs once: the gate counts)
 *   metal-wm-test fork          a child after fork makes calls through a context of its own; the parent's still work
 *   metal-wm-test cross-thread  the ordering rules across rings (Review Focus 1), with the host started with
 *                               --metal-test-ops: a fence made on one thread's ring while the host holds that ring in
 *                               a 200 ms MN_OP_TEST_DELAY, and used at once on another's; and a buffer released on one
 *                               ring while another, held the same way, still has a blit from it queued */
#include <dlfcn.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../calls.h"
#include "galloc-cases.h"
#include "sequence.h"
#include "winemetal_thunks.h"

typedef NTSTATUS_T (*entry_t)(void *params);
static const entry_t *funcs;

static NTSTATUS_T call(unsigned slot, void *params) { return funcs[slot](params); }
static void print(const char *line) {
    printf("%s\n", line);
    fflush(stdout);
}

/* ---- cross-thread ---- */
#define LAG_NS 200000000ull /* MN_OP_TEST_DELAY: the host busy-waits this long on the ring */
#define WORDS 4096u
static int (*send_op)(uint32_t op, const void *p, uint32_t len); /* the transport's mn_send, in winemetal.so */
static obj_handle_t device, queue, fence, src, dst;
static uint32_t *src_words, *dst_words;
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int stage;

static void lag(void) { /* holds this thread's ring on the host, queued */
    uint64_t ns = LAG_NS;
    send_op(0x105 /* MN_OP_TEST_DELAY */, &ns, sizeof ns);
}
static void set_stage(int s) {
    pthread_mutex_lock(&m);
    stage = s;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&m);
}
static void wait_stage(int s) {
    pthread_mutex_lock(&m);
    while (stage < s) pthread_cond_wait(&cv, &m);
    pthread_mutex_unlock(&m);
}
static obj_handle_t buffer(uint32_t **words) {
    struct WMTBufferInfo info = {.length = WORDS * 4, .options = WMTResourceStorageModeShared};
    struct unixcall_mtldevice_newbuffer b = {device, {&info}, 0};
    call(18, &b);
    *words = info.memory.ptr;
    return b.ret;
}
/* One command buffer on this thread: a blit copying src into dst, waiting on fence if there is one. */
static int blit(obj_handle_t f) {
    struct unixcall_generic_obj_ret pool = {0};
    call(10, &pool);
    struct unixcall_generic_obj_obj_ret cb = {queue, 0}, enc;
    call(11, &cb);
    enc = (struct unixcall_generic_obj_obj_ret){cb.ret, 0};
    call(30, &enc);
    struct wmtcmd_blit_copy_from_buffer_to_buffer copy = {.type = WMTBlitCommandCopyFromBufferToBuffer, .src = src,
                                                          .dst = dst, .copy_length = WORDS * 4};
    struct wmtcmd_blit_fence_op update = {.type = WMTBlitCommandUpdateFence, .next = {&copy}, .fence = f};
    struct unixcall_generic_obj_cmd_noret e = {enc.ret, {f ? (void *)&update : (void *)&copy}};
    struct unixcall_generic_obj_noret end = {enc.ret}, c = {cb.ret};
    call(36, &e);
    call(33, &end);
    call(12, &c);
    set_stage(2); /* sent: the other thread goes on */
    call(13, &c); /* waits for this ring to catch up */
    struct unixcall_generic_obj_uint64_ret st = {cb.ret, 0};
    call(14, &st);
    call(1, &pool.ret);
    return st.ret == 4;
}
static void *fence_user(void *p) {
    (void)p;
    wait_stage(1);
    int ok = blit(fence);
    return (void *)(uintptr_t)ok;
}
static void *lagging_user(void *p) {
    (void)p;
    lag();
    int ok = blit(0);
    return (void *)(uintptr_t)ok;
}
static int check_copy(const char *what) {
    for (uint32_t i = 0; i < WORDS; i++)
        if (dst_words[i] != src_words[i]) {
            printf("FAIL cross-thread %s: word %u\n", what, i);
            return 0;
        }
    return 1;
}
static int cross_thread(int (*stats)(struct mn_wm_stats *)) {
    struct unixcall_generic_obj_ret all = {0};
    call(4, &all);
    struct unixcall_generic_obj_uint64_obj_ret first = {all.ret, 0, 0};
    call(2, &first);
    device = first.ret;
    struct unixcall_generic_obj_uint64_obj_ret q = {device, 64, 0};
    call(9, &q);
    queue = q.ret;
    int ok = 1;
    /* A use before the binding: the fence is made behind LAG queued calls here, and used at once over there. */
    src = buffer(&src_words), dst = buffer(&dst_words);
    for (uint32_t i = 0; i < WORDS; i++) src_words[i] = i * 2654435761u;
    pthread_t t;
    pthread_create(&t, NULL, fence_user, NULL);
    lag();
    struct unixcall_generic_obj_obj_ret nf = {device, 0};
    call(105, &nf);
    fence = nf.ret;
    set_stage(1);
    void *r;
    pthread_join(t, &r);
    ok &= (int)(uintptr_t)r && check_copy("fence");
    call(1, &fence);
    /* A release before a queued use: that thread's blit from src waits behind LAG calls when src is released here. */
    for (uint32_t i = 0; i < WORDS; i++) src_words[i] = ~i, dst_words[i] = 0;
    stage = 0;
    pthread_create(&t, NULL, lagging_user, NULL);
    wait_stage(2);
    uint32_t *kept = src_words; /* the guest's memory stays until the host frees the region */
    call(1, &src);
    pthread_join(t, &r);
    src_words = kept;
    ok &= (int)(uintptr_t)r && check_copy("release");
    call(1, &dst);
    call(1, &queue);
    call(1, &all.ret);
    struct mn_wm_stats st;
    if (stats(&st)) st.failures = ~0ull;
    if (st.failures) printf("FAIL cross-thread: %llu failed calls\n", (unsigned long long)st.failures);
    ok &= !st.failures;
    if (ok) printf("cross-thread ok\n");
    return !ok;
}

/* ---- refused, fork ---- */
static int refused(void) {
    /* Mach ports and slot 83 (milestone 4c forwards the presentation slots 4b-1 refused). */
    static const unsigned unsupported[] = {120, 121, 122, 123, 124, 83};
    uint8_t params[256] = {0};
    int ok = 1;
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < sizeof unsupported / sizeof *unsupported; i++)
            ok &= call(unsupported[i], params) == 0xC00000BBu; /* STATUS_NOT_SUPPORTED */
    }
    printf(ok ? "refused ok\n" : "FAIL refused\n");
    return !ok;
}

static int fork_case(void) {
    struct unixcall_generic_obj_ret all = {0};
    call(4, &all);
    pid_t pid = fork();
    if (!pid) {
        struct unixcall_generic_obj_ret mine = {0};
        call(4, &mine);
        struct unixcall_generic_obj_uint64_ret count = {mine.ret, 0};
        _exit(call(3, &count) == 0 && count.ret ? 0 : 1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    struct unixcall_generic_obj_uint64_ret count = {all.ret, 0};
    int ok = pid > 0 && WIFEXITED(st) && !WEXITSTATUS(st) && call(3, &count) == 0 && count.ret;
    printf(ok ? "fork ok\n" : "FAIL fork\n");
    return !ok;
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
    send_op = (int (*)(uint32_t, const void *, uint32_t))dlsym(wm, "mn_send");
    if (argc == 2 && !strcmp(argv[1], "cross-thread")) return send_op ? cross_thread(stats) : 2;
    if (argc == 2 && !strcmp(argv[1], "refused")) return refused();
    if (argc == 2 && !strcmp(argv[1], "fork")) return fork_case();
    if (argc == 2 && !strcmp(argv[1], "galloc")) {
        struct mn_galloc_env g = {call, stats, print};
        return mn_galloc_cases(&g) ? 1 : 0;
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
