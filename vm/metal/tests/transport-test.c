/* Milestone 4a Task 3 test in the guest: the Metal transport (guest/transport.c) against the executor's test ops.
 *   metal-transport-test order | blob | free-in-flight | cross-free | gap [bursts] | threads <n> | invalid | errors | lost-wake |
 *                        ring-cap | exit-drain | bench      (the host started with --metal-test-ops)
 *   metal-transport-test trap | trap-cross | trap-bench       (drained rings, M2: the host with --metal-test-ops)
 *   metal-transport-test no-test-ops                          (the host started without it)
 * Every case but errors also requires that no asynchronous record failed on the host (MN_OP_TEST_ERRORS).
 * A case prints "<case> ok" (bench prints its numbers first), or "FAIL <case>: <why>" and exits 1. */
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../proto.h"
#include "../guest/transport.h"

static int failf(const char *c, const char *why, long long a, long long b) {
    printf("FAIL %s: %s (%lld, %lld)\n", c, why, a, b);
    exit(1);
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median3(double a, double b, double c) {
    double v[3] = { a, b, c };
    qsort(v, 3, sizeof *v, cmp_d);
    return v[1];
}

static void *order_thread(void *arg) {
    (void)arg;
    for (uint64_t i = 1; i <= 10000; i++) {
        uint64_t got = 0;
        uint32_t n = 0;
        if (mn_send(MN_OP_TEST_SET, &i, sizeof i)) failf("order", "send", (long long)i, 0);
        if (mn_call(MN_OP_TEST_GET, NULL, 0, &got, sizeof got, &n) || n != sizeof got || got != i)
            failf("order", "GET after SET", (long long)got, (long long)i);
    }
    return NULL;
}

static void order(void) {
    pthread_t t[8];
    for (int i = 0; i < 8; i++) pthread_create(&t[i], NULL, order_thread, NULL);
    for (int i = 0; i < 8; i++) pthread_join(t[i], NULL);
    puts("order ok");
}

static void blob(void) {
    struct mn_blob b;
    if (mn_blob_alloc(65536, &b)) failf("blob", "alloc", 0, 0);
    uint8_t *p = b.ptr;
    uint64_t want = 0, got = 0;
    uint32_t n = 0;
    for (int i = 0; i < 65536; i++) p[i] = (uint8_t)(i * 31), want += p[i];
    struct mn_test_sum s = { b.blob_id, 0, 65536 };
    if (mn_call(MN_OP_TEST_SUM, &s, sizeof s, &got, sizeof got, &n) || got != want)
        failf("blob", "host sum of guest bytes", (long long)got, (long long)want);
    struct mn_test_fill f = { b.blob_id, 100, 1000, 0x5a, 0 };
    if (mn_call(MN_OP_TEST_FILL, &f, sizeof f, NULL, 0, &n)) failf("blob", "fill", 0, 0);
    for (int i = 0; i < 65536; i++)
        if (p[i] != (i >= 100 && i < 1100 ? 0x5a : (uint8_t)(i * 31))) failf("blob", "guest view of host fill", i, p[i]);
    if (mn_blob_free(&b)) failf("blob", "free", 0, 0);
    puts("blob ok");
}

static void free_in_flight(void) {
    for (int i = 0; i < 1000; i++) {
        struct mn_blob b;
        if (mn_blob_alloc(MN_PAGE, &b)) failf("free-in-flight", "alloc", i, 0);
        struct mn_test_fill f = { b.blob_id, 0, MN_PAGE, 7, 0 };
        if (mn_send(MN_OP_TEST_FILL, &f, sizeof f)) failf("free-in-flight", "send", i, 0);
        if (mn_blob_free(&b)) failf("free-in-flight", "free", i, 0);
    }
    uint32_t n;
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("free-in-flight", "call after", 0, 0);
    puts("free-in-flight ok");
}

static void gap(int bursts) {
    srand(11);
    for (uint64_t b = 0; b < (uint64_t)bursts; b++) {
        for (uint64_t k = 0; k < 1000; k++)
            if (mn_send(MN_OP_TEST_SET, &k, sizeof k)) failf("gap", "send", (long long)b, (long long)k);
        usleep(1500 + rand() % 1000);
    }
    uint64_t sent, done;
    double t0 = now_us();
    do {
        mn_ring_stats(&sent, &done);
        if (done == sent) break;
        usleep(100);
    } while (now_us() - t0 < 100000);
    if (done != sent) failf("gap", "records still pending 100 ms after the last send", (long long)done, (long long)sent);
    puts("gap ok");
}

static pthread_barrier_t threads_barrier;
static atomic_int polling;

static void *threads_thread(void *arg) {
    unsigned seed = (unsigned)(uintptr_t)arg;
    uint8_t in[256], out[256];
    uint32_t n0;
    /* Every thread holds its ring before any starts its loop, so all of them are attached at once. */
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n0)) failf("threads", "first call", 0, 0);
    if (mn_ring_fenced() == 0) atomic_fetch_add(&polling, 1);
    pthread_barrier_wait(&threads_barrier);
    for (int i = 0; i < 1000; i++) {
        uint32_t len = (uint32_t)(rand_r(&seed) % 257), n = 0;
        for (uint32_t j = 0; j < len; j++) in[j] = (uint8_t)rand_r(&seed);
        if (mn_call(MN_OP_TEST_ECHO, in, len, out, sizeof out, &n) || n != len || memcmp(in, out, len))
            failf("threads", "echo", i, (long long)n);
    }
    return NULL;
}

static void threads(int count) {
    pthread_t *t = calloc((size_t)count, sizeof *t);
    pthread_barrier_init(&threads_barrier, NULL, (unsigned)count);
    for (int i = 0; i < count; i++) pthread_create(&t[i], NULL, threads_thread, (void *)(uintptr_t)(i + 1));
    for (int i = 0; i < count; i++) pthread_join(t[i], NULL);
    /* 63 fence ring indexes: beyond that, threads wait by polling. */
    if (count > 63 && atomic_load(&polling) != count - 63) failf("threads", "threads polling", atomic_load(&polling), count - 63);
    printf("threads: %d polling\n", atomic_load(&polling));
    puts("threads ok");
}

static void invalid(void) {
    uint32_t n;
    if (mn_send(MN_OP_WRAP, NULL, 0) != -EINVAL) failf("invalid", "op 0 sent", 0, 0);
    if (mn_send(MN_REC_SYNC | MN_OP_NOP, NULL, 0) != -EINVAL) failf("invalid", "the sync bit sent", 0, 0);
    if (mn_call(MN_OP_WRAP, NULL, 0, NULL, 0, &n) != -EINVAL) failf("invalid", "op 0 called", 0, 0);
    puts("invalid ok");
}

static struct mn_blob held;

static void *cross_hold(void *arg) {
    (void)arg;
    uint32_t n;
    struct mn_test_hold h = { held.blob_id, 50000000 }; /* the host writes the blob for 50 ms */
    if (mn_call(MN_OP_TEST_HOLD, &h, sizeof h, NULL, 0, &n)) failf("cross-free", "hold", 0, 0);
    return NULL;
}

/* Another thread frees a blob while this one's host op is writing it (through its own ring): the host must finish the
 * op before the blob's memory goes. */
static void cross_free(void) {
    uint32_t n;
    if (mn_blob_alloc(MN_PAGE, &held)) failf("cross-free", "alloc", 0, 0);
    pthread_t t;
    pthread_create(&t, NULL, cross_hold, NULL);
    usleep(10000);
    if (mn_blob_free(&held)) failf("cross-free", "free", 0, 0);
    pthread_join(t, NULL);
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("cross-free", "call after", 0, 0);
    puts("cross-free ok");
}

/* A doorbell that went out and was lost: the ring reads "parked, woken" (2) while its consumer still sleeps. The next
 * call's fence wait must wake it. */
static void lost_wake(void) {
    uint32_t n;
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("lost-wake", "first call", 0, 0);
    usleep(5000); /* the consumer parks after 2 ms */
    atomic_store(&mn_ring_header()->parked, 2);
    alarm(3);
    double t0 = now_us();
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("lost-wake", "call", 0, 0);
    double us = now_us() - t0;
    alarm(0);
    if (us > 500000) failf("lost-wake", "call took (us)", (long long)us, 500000);
    puts("lost-wake ok");
}

#define RING_THREADS 300
static pthread_barrier_t cap_barrier;
static atomic_int cap_ok, cap_refused, cap_fenced;

static void *cap_thread(void *arg) {
    (void)arg;
    uint32_t n;
    int r = mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    if (r == 0) atomic_fetch_add(&cap_ok, 1);
    else if (r == -ENODEV) atomic_fetch_add(&cap_refused, 1);
    else failf("ring-cap", "call", r, 0);
    pthread_barrier_wait(&cap_barrier); /* every ring is held at once */
    return NULL;
}

static void *fenced_thread(void *arg) {
    (void)arg;
    uint32_t n;
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("ring-cap", "call after", 0, 0);
    if (mn_ring_fenced() == 1) atomic_fetch_add(&cap_fenced, 1);
    pthread_barrier_wait(&cap_barrier);
    return NULL;
}

/* The host attaches at most 256 rings per context and says so when it refuses one; leased fence indexes come back. */
static void ring_cap(void) {
    pthread_t t[RING_THREADS];
    alarm(60);
    pthread_barrier_init(&cap_barrier, NULL, RING_THREADS);
    for (int i = 0; i < RING_THREADS; i++) pthread_create(&t[i], NULL, cap_thread, NULL);
    for (int i = 0; i < RING_THREADS; i++) pthread_join(t[i], NULL);
    if (atomic_load(&cap_ok) != 256 || atomic_load(&cap_refused) != RING_THREADS - 256)
        failf("ring-cap", "rings attached and refused", atomic_load(&cap_ok), atomic_load(&cap_refused));
    pthread_barrier_destroy(&cap_barrier);
    pthread_barrier_init(&cap_barrier, NULL, 63);
    for (int i = 0; i < 63; i++) pthread_create(&t[i], NULL, fenced_thread, NULL);
    for (int i = 0; i < 63; i++) pthread_join(t[i], NULL);
    if (atomic_load(&cap_fenced) != 63) failf("ring-cap", "fence indexes back after the refusals", atomic_load(&cap_fenced), 63);
    alarm(0);
    puts("ring-cap ok");
}

static atomic_int drain_queued;
static struct mn_blob drain_blob;

static void *drain_thread(void *arg) {
    (void)arg;
    struct mn_test_hold h = { drain_blob.blob_id, 5000000 };
    for (int i = 0; i < 400; i++) /* 2 s of host work */
        if (mn_send(MN_OP_TEST_HOLD, &h, sizeof h)) failf("exit-drain", "send", i, 0);
    atomic_store(&drain_queued, 1);
    return NULL; /* exits with its ring full */
}

/* A thread that exits with work queued must not stall the other threads' control messages while its ring drains. */
static void exit_drain(void) {
    uint32_t n;
    if (mn_blob_alloc(MN_PAGE, &drain_blob)) failf("exit-drain", "alloc", 0, 0);
    if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("exit-drain", "first call", 0, 0);
    pthread_t t;
    pthread_create(&t, NULL, drain_thread, NULL);
    while (!atomic_load(&drain_queued)) usleep(100);
    double worst = 0;
    for (int i = 0; i < 20; i++) {
        usleep(5000); /* this thread's consumer parks: each call needs a doorbell */
        double t0 = now_us();
        if (mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n)) failf("exit-drain", "call", i, 0);
        if (now_us() - t0 > worst) worst = now_us() - t0;
    }
    pthread_join(t, NULL);
    if (mn_blob_free(&drain_blob)) failf("exit-drain", "free", 0, 0);
    if (worst > 200000) failf("exit-drain", "slowest call while another thread drained (us)", (long long)worst, 200000);
    puts("exit-drain ok");
}

static void no_test_ops(void) {
    uint8_t b = 1;
    uint32_t n;
    int r = mn_call(MN_OP_TEST_ECHO, &b, 1, &b, 1, &n);
    if (r != MN_ENOSYS) failf("no-test-ops", "a test op without --metal-test-ops", r, MN_ENOSYS);
    puts("no-test-ops ok");
}

static uint64_t host_errors(void) {
    uint64_t count = ~0ull;
    uint32_t n;
    if (mn_call(MN_OP_TEST_ERRORS, NULL, 0, &count, sizeof count, &n) || n != sizeof count) return ~0ull;
    return count;
}

static void errors(void) {
    struct mn_test_fill f = { 999999, 0, 1, 0, 0 }; /* no such blob: fails on the host, asynchronously */
    if (mn_send(MN_OP_TEST_FILL, &f, sizeof f)) failf("errors", "send", 0, 0);
    uint64_t count = host_errors();
    if (count != 1) failf("errors", "host's count of failed asynchronous records", (long long)count, 1);
    puts("errors ok");
}

static double async_ns(void) {
    uint32_t n;
    mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    double t0 = now_us();
    for (int i = 0; i < 1000000; i++) mn_send(MN_OP_NOP, NULL, 0);
    mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    return (now_us() - t0) * 1000 / 1e6;
}

static double sync_ns(int calls) {
    uint32_t n;
    double t0 = now_us();
    for (int i = 0; i < calls; i++) mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    return (now_us() - t0) * 1000 / calls;
}

/* A call the guest waits for on its fence: a 200 µs host op with no spin, less the 200 µs. */
static double fenced_us(void) {
    double v[100];
    uint64_t ns = 200000;
    uint32_t n;
    for (int i = 0; i < 100; i++) {
        double t0 = now_us();
        mn_call(MN_OP_TEST_DELAY, &ns, sizeof ns, NULL, 0, &n);
        v[i] = now_us() - t0 - 200;
    }
    qsort(v, 100, sizeof *v, cmp_d);
    return v[50];
}

static double wake_us(void) {
    double v[20];
    uint32_t n;
    for (int i = 0; i < 20; i++) {
        usleep(5000);
        double t0 = now_us();
        mn_call(MN_OP_NOP, NULL, 0, NULL, 0, &n);
        v[i] = now_us() - t0;
    }
    qsort(v, 20, sizeof *v, cmp_d);
    return v[10];
}

static void bench(void) {
    printf("async ns/cmd %.1f\n", median3(async_ns(), async_ns(), async_ns()));
    printf("sync spin ns %.0f\n", median3(sync_ns(100000), sync_ns(100000), sync_ns(100000)));
    mn_set_spin_us(0);
    printf("sync fenced us %.1f\n", median3(fenced_us(), fenced_us(), fenced_us()));
    mn_set_spin_us(50);
    printf("wake us %.1f\n", median3(wake_us(), wake_us(), wake_us()));
    puts("bench ok");
}

/* Drained rings: 10,000 queued records, then a call by flush answers after them, its reply present at once. */
static void trap_order(void) {
    if (mn_drained()) failf("trap", "mn_drained", 0, 0);
    for (uint64_t i = 1; i <= 10000; i++)
        if (mn_queue(MN_OP_TEST_SET, &i, sizeof i)) failf("trap", "queue", (long long)i, 0);
    uint64_t got = 0, sent, done;
    uint32_t n = 0;
    int r = mn_call_flush(MN_OP_TEST_GET, NULL, 0, &got, sizeof got, &n);
    mn_ring_stats(&sent, &done);
    if (r || n != sizeof got || got != 10000 || done != sent) failf("trap", "GET after 10,000 SETs", (long long)got, r);
    puts("trap ok");
}

/* Thread A queues 100 fills of a shared blob and doesn't flush; thread B's queue operation sees them run first. */
static struct mn_blob cross_blob;
static atomic_int cross_state; /* 1: A queued; 2: B checked */
static void *cross_a(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < 100; i++) {
        struct mn_test_fill f = { cross_blob.blob_id, i, 1, i + 1, 0 };
        if (mn_queue(MN_OP_TEST_FILL, &f, sizeof f)) failf("trap-cross", "queue", i, 0);
    }
    cross_state = 1;
    while (cross_state != 2) usleep(1000); /* the thread's exit would flush its ring */
    return NULL;
}
static void trap_cross(void) {
    if (mn_drained() || mn_blob_alloc(MN_PAGE, &cross_blob)) failf("trap-cross", "setup", 0, 0);
    memset(cross_blob.ptr, 0, MN_PAGE);
    pthread_t a;
    pthread_create(&a, NULL, cross_a, NULL);
    while (cross_state != 1) usleep(1000);
    struct mn_test_sum s = { cross_blob.blob_id, 0, 100 };
    uint64_t sum = 0;
    uint32_t n = 0;
    if (((uint8_t *)cross_blob.ptr)[0]) failf("trap-cross", "ran before any flush", 0, 0);
    int r = mn_call_flush(MN_OP_TEST_SUM | MN_REC_SUBMIT, &s, sizeof s, &sum, sizeof sum, &n);
    cross_state = 2;
    pthread_join(a, NULL);
    if (r || sum != 5050) failf("trap-cross", "the submit's sum of A's fills", (long long)sum, 5050);
    if (mn_blob_free(&cross_blob)) failf("trap-cross", "free", 0, 0);
    puts("trap-cross ok");
}

/* A synchronous call by trap (one store, the host runs it on the vCPU's thread), once over 100,000 calls. */
static void trap_bench(void) {
    if (mn_drained()) failf("trap-bench", "mn_drained", 0, 0);
    uint32_t n;
    mn_call_flush(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    double t0 = now_us();
    for (int i = 0; i < 100000; i++) mn_call_flush(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    printf("trap sync ns %.0f\n", (now_us() - t0) * 1000 / 100000);
    t0 = now_us();
    for (int i = 0; i < 1000000; i++) mn_queue(MN_OP_NOP, NULL, 0);
    mn_call_flush(MN_OP_NOP, NULL, 0, NULL, 0, &n);
    printf("trap async ns/cmd %.1f\n", (now_us() - t0) * 1000 / 1e6);
    puts("trap-bench ok");
}

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: %s <case> (see the top of transport-test.c)\n", argv[0]), 2;
    if (mn_open()) return printf("FAIL %s: mn_open\n", argv[1]), 1;
    if (!strcmp(argv[1], "order")) order();
    else if (!strcmp(argv[1], "blob")) blob();
    else if (!strcmp(argv[1], "free-in-flight")) free_in_flight();
    else if (!strcmp(argv[1], "gap")) gap(argc > 2 ? atoi(argv[2]) : 100);
    else if (!strcmp(argv[1], "threads") && argc > 2) threads(atoi(argv[2]));
    else if (!strcmp(argv[1], "cross-free")) cross_free();
    else if (!strcmp(argv[1], "invalid")) invalid();
    else if (!strcmp(argv[1], "errors")) return errors(), 0;
    else if (!strcmp(argv[1], "lost-wake")) lost_wake();
    else if (!strcmp(argv[1], "ring-cap")) ring_cap();
    else if (!strcmp(argv[1], "exit-drain")) exit_drain();
    else if (!strcmp(argv[1], "no-test-ops")) return no_test_ops(), 0;
    else if (!strcmp(argv[1], "bench")) bench();
    else if (!strcmp(argv[1], "trap")) trap_order();
    else if (!strcmp(argv[1], "trap-cross")) trap_cross();
    else if (!strcmp(argv[1], "trap-bench")) trap_bench();
    else return fprintf(stderr, "unknown case %s\n", argv[1]), 2;
    uint64_t count = host_errors();
    if (count) return printf("FAIL %s: %lld asynchronous records failed on the host\n", argv[1], (long long)count), 1;
    return 0;
}
