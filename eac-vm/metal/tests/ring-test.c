/* The command ring (ring.c) on the Mac, without a VM: one producer thread and one consumer thread over a ring blob.
 *   records  1M records of 0 to 4,096 payload bytes through many wraps, checked in order and byte for byte
 *   park     200 bursts with 0-3 ms gaps; the consumer parks between them and is woken through the doorbell callback;
 *            the last burst completes without any extra wake */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "../ring.h"

#define N_RECORDS 1000000u
static struct mn_ring_view pv, cv; /* each side's own view (its caches) of one ring blob */
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t c = PTHREAD_COND_INITIALIZER;
static int wake_pending, wakes;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void wake(void *ctx) {
    (void)ctx;
    pthread_mutex_lock(&m);
    wake_pending = 1;
    wakes++;
    pthread_cond_signal(&c);
    pthread_mutex_unlock(&m);
}

static uint32_t len_of(uint64_t i) { return (uint32_t)((i * 7919u) % 4097u); }

static void put(uint64_t *head, uint64_t seq, uint32_t len) {
    struct mn_rec *r;
    while (!(r = mn_ring_reserve(&pv, head, len))) sched_yield();
    r->op = 0x100 + (uint32_t)(seq % 7);
    r->seq = seq;
    for (uint32_t j = 0; j < len; j++) ((uint8_t *)(r + 1))[j] = (uint8_t)(seq + j);
    mn_ring_publish(&pv, *head, wake, NULL);
}

static uint64_t consumed;
static int stop;

/* parks: 0 = spin only; otherwise park after 0.5 ms idle and sleep until woken */
static void *consumer(void *parks) {
    uint64_t tail = 0, idle = now_ns();
    for (;;) {
        const struct mn_rec *r = mn_ring_next(&cv, &tail);
        if (r) {
            assert(r->seq == consumed + 1);
            assert(r->op == 0x100 + (uint32_t)(r->seq % 7));
            for (uint32_t j = 0; j < r->len; j++) assert(((const uint8_t *)(r + 1))[j] == (uint8_t)(r->seq + j));
            consumed = r->seq;
            mn_ring_done(&cv, &tail, r);
            idle = now_ns();
            continue;
        }
        if (__atomic_load_n(&stop, __ATOMIC_ACQUIRE) && !mn_ring_next(&cv, &tail)) return NULL;
        if (!parks || now_ns() - idle < 500000) continue;
        if (!mn_ring_try_park(&cv)) continue;
        pthread_mutex_lock(&m);
        while (!wake_pending && !__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) pthread_cond_wait(&c, &m);
        wake_pending = 0;
        pthread_mutex_unlock(&m);
        mn_ring_unpark(&cv);
        idle = now_ns();
    }
}

static void reset(void) {
    void *blob = pv.hdr;
    memset(blob, 0, sizeof *pv.hdr);
    mn_ring_view_init(&pv, blob);
    mn_ring_view_init(&cv, blob);
    consumed = 0;
    stop = 0;
    wake_pending = 0;
    wakes = 0;
}

static void finish(pthread_t t) {
    __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
    wake(NULL);
    pthread_join(t, NULL);
}

static void records(void) {
    reset();
    pthread_t t;
    pthread_create(&t, NULL, consumer, (void *)0);
    uint64_t head = 0;
    for (uint64_t i = 1; i <= N_RECORDS; i++) put(&head, i, len_of(i));
    while (atomic_load_explicit(&pv.hdr->done_seq, memory_order_acquire) != N_RECORDS) sched_yield();
    finish(t);
    assert(consumed == N_RECORDS);
    assert(head > 100ull * MN_RING_BYTES);
    printf("records ok (%u records, %llu ring laps)\n", N_RECORDS, (unsigned long long)(head / MN_RING_BYTES));
}

static void park(void) {
    reset();
    pthread_t t;
    pthread_create(&t, NULL, consumer, (void *)1);
    uint64_t head = 0, seq = 0;
    srand(7);
    for (int b = 0; b < 200; b++) {
        int n = 1 + rand() % 50;
        for (int k = 0; k < n; k++) seq++, put(&head, seq, len_of(seq) % 512);
        usleep((useconds_t)(rand() % 3000));
    }
    uint64_t t0 = now_ns();
    while (atomic_load_explicit(&pv.hdr->done_seq, memory_order_acquire) != seq && now_ns() - t0 < 100000000ull) usleep(100);
    assert(atomic_load_explicit(&pv.hdr->done_seq, memory_order_acquire) == seq);
    finish(t);
    assert(wakes > 0 && wakes <= 400); /* one doorbell per park, not one per record */
    printf("park ok (%llu records, %d wakes)\n", (unsigned long long)seq, wakes);
}

int main(void) {
    void *blob = mmap(NULL, MN_RING_BLOB_BYTES, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    assert(blob != MAP_FAILED);
    mn_ring_view_init(&pv, blob);
    records();
    park();
    return 0;
}
