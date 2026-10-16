/* Milestone 4a Task 3 test in the guest: the Metal transport (guest/transport.c) against the executor's test ops.
 *   metal-transport-test order | blob | free-in-flight | gap | threads <n> | bench
 * A case prints "<case> ok" (bench prints its numbers first), or "FAIL <case>: <why>" and exits 1. */
#include <pthread.h>
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

static void gap(void) {
    srand(11);
    for (uint64_t b = 0; b < 100; b++) {
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

static void *threads_thread(void *arg) {
    unsigned seed = (unsigned)(uintptr_t)arg;
    uint8_t in[256], out[256];
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
    for (int i = 0; i < count; i++) pthread_create(&t[i], NULL, threads_thread, (void *)(uintptr_t)(i + 1));
    for (int i = 0; i < count; i++) pthread_join(t[i], NULL);
    puts("threads ok");
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

int main(int argc, char **argv) {
    if (argc < 2) return fprintf(stderr, "usage: %s order|blob|free-in-flight|gap|threads <n>|bench\n", argv[0]), 2;
    if (mn_open()) return printf("FAIL %s: mn_open\n", argv[1]), 1;
    if (!strcmp(argv[1], "order")) order();
    else if (!strcmp(argv[1], "blob")) blob();
    else if (!strcmp(argv[1], "free-in-flight")) free_in_flight();
    else if (!strcmp(argv[1], "gap")) gap();
    else if (!strcmp(argv[1], "threads") && argc > 2) threads(atoi(argv[2]));
    else if (!strcmp(argv[1], "bench")) bench();
    else return fprintf(stderr, "unknown case %s\n", argv[1]), 2;
    return 0;
}
