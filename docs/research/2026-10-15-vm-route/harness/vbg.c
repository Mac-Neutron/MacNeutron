// Guest side of the transport benchmark: doorbell round trips through /dev/vbench, then the same ring and fence runs as
// the host baseline, producing into the shared memory region a host thread consumes.
#include "ring.h"
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static void push(struct vb_shm *s, uint64_t *head, uint64_t *tail_cache, uint32_t kind, uint64_t v) {
  while (*head - *tail_cache == VB_RING) *tail_cache = __atomic_load_n(&s->tail, __ATOMIC_ACQUIRE);
  struct vb_cmd *c = &s->slot[*head % VB_RING];
  c->kind = kind, c->a = 0, c->value = v;
  for (int i = 0; i < 6; i++) c->payload[i] = v + i;
  __atomic_store_n(&s->head, ++*head, __ATOMIC_RELEASE);
}
int main(void) {
  int fd = open("/dev/vbench", O_RDWR);
  if (fd < 0) { perror("/dev/vbench"); return 1; }
  long n = 100000;
  long us = ioctl(fd, 1, n); printf("doorbell round trip (interrupt): %.2f us\n", us * 1.0 / n);
  us = ioctl(fd, 2, n); printf("doorbell round trip (polled): %.2f us\n", us * 1.0 / n);
  struct vb_shm *s = mmap(NULL, sizeof *s, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (s == MAP_FAILED) { perror("mmap"); return 1; }
  uint64_t head = s->head, tail = s->tail, cmds = 50000000;
  double t0 = now();
  for (uint64_t i = 0; i < cmds; i++) push(s, &head, &tail, 0, i);
  while (__atomic_load_n(&s->tail, __ATOMIC_ACQUIRE) != head) ;
  double t = now() - t0;
  printf("guest->host ring: %.1f ns/cmd (%.1f M cmd/s, %.2f GB/s)\n", t / cmds * 1e9, cmds / t / 1e6, cmds * 64.0 / t / 1e9);
  uint64_t fences = 1000000, base = s->fence_done; t0 = now();
  for (uint64_t i = 1; i <= fences; i++) {
    push(s, &head, &tail, 1, base + i);
    while (__atomic_load_n(&s->fence_done, __ATOMIC_ACQUIRE) != base + i) ;
  }
  t = now() - t0;
  printf("guest->host fence round trip: %.1f ns\n", t / fences * 1e9);
  return 0;
}
