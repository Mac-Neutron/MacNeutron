// Host side: the consumer thread over a vb_shm, and an in-process baseline (a host producer thread, no VM).
#include "ring.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/qos.h>
static void *consume(void *arg) {
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  struct vb_shm *s = arg;
  uint64_t tail = __atomic_load_n(&s->tail, __ATOMIC_RELAXED), sum = 0;
  while (!__atomic_load_n(&s->stop, __ATOMIC_RELAXED)) {
    uint64_t head = __atomic_load_n(&s->head, __ATOMIC_ACQUIRE);
    if (head == tail) { __asm__ volatile("yield"); continue; }
    for (; tail < head; tail++) {
      struct vb_cmd *c = &s->slot[tail % VB_RING];
      sum += c->value + c->payload[0] + c->payload[5];
      if (c->kind == 1) __atomic_store_n(&s->fence_done, c->value, __ATOMIC_RELEASE);
      __atomic_store_n(&s->tail, tail + 1, __ATOMIC_RELEASE);
    }
    s->sum = sum;
  }
  return NULL;
}
void vb_start_consumer(void *shm) {
  pthread_t t;
  pthread_create(&t, NULL, consume, shm);
  pthread_detach(t);
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
// The producer half, shared with the guest tool through a copy (vbench_guest.c has the same code).
static void push(struct vb_shm *s, uint64_t *head, uint64_t *tail_cache, uint32_t kind, uint64_t v) {
  while (*head - *tail_cache == VB_RING) *tail_cache = __atomic_load_n(&s->tail, __ATOMIC_ACQUIRE);
  struct vb_cmd *c = &s->slot[*head % VB_RING];
  c->kind = kind, c->a = 0, c->value = v;
  for (int i = 0; i < 6; i++) c->payload[i] = v + i;
  __atomic_store_n(&s->head, ++*head, __ATOMIC_RELEASE);
}
void vb_baseline(void) {
  struct vb_shm *s; if (posix_memalign((void **)&s, 16384, sizeof *s)) return;
  memset(s, 0, sizeof *s);
  vb_start_consumer(s);
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  uint64_t head = 0, tail = 0, n = 50000000;
  double t0 = now();
  for (uint64_t i = 0; i < n; i++) push(s, &head, &tail, 0, i);
  while (__atomic_load_n(&s->tail, __ATOMIC_ACQUIRE) != head) ;
  double t = now() - t0;
  printf("host-baseline ring: %.1f ns/cmd (%.1f M cmd/s, %.2f GB/s)\n", t / n * 1e9, n / t / 1e6, n * 64.0 / t / 1e9);
  uint64_t fences = 1000000; t0 = now();
  for (uint64_t i = 1; i <= fences; i++) {
    push(s, &head, &tail, 1, i);
    while (__atomic_load_n(&s->fence_done, __ATOMIC_ACQUIRE) != i) ;
  }
  t = now() - t0;
  printf("host-baseline fence round trip: %.1f ns\n", t / fences * 1e9);
  s->stop = 1;
}
