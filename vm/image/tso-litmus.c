// tso-litmus <ms>: message passing for <ms> milliseconds. The writer stores data, then flag; the reader loads flag,
// then data. data < flag (the flag's store seen before the data's) is forbidden under TSO and allowed on Arm's weak
// memory model. Prints "violations <n> of <m>"; exits 0 only when n == 0 and m >= 1000000.
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static volatile long data __attribute__((aligned(128)));
static volatile long flag __attribute__((aligned(128)));
static atomic_int stop;
static void *writer(void *arg) {
  (void)arg;
  for (long i = 1; !atomic_load_explicit(&stop, memory_order_relaxed); i++) { data = i; flag = i; }
  return 0;
}
static long elapsed_ms(const struct timespec *s) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - s->tv_sec) * 1000 + (now.tv_nsec - s->tv_nsec) / 1000000;
}
int main(int argc, char **argv) {
  long ms = argc > 1 ? atol(argv[1]) : 200, bad = 0, n = 0;
  struct timespec start;
  pthread_t t;
  pthread_create(&t, 0, writer, 0);
  clock_gettime(CLOCK_MONOTONIC, &start);
  do {
    for (int k = 0; k < 4096; k++) { long f = flag; long d = data; if (d < f) bad++; n++; }
  } while (elapsed_ms(&start) < ms);
  atomic_store(&stop, 1);
  pthread_join(t, 0);
  printf("violations %ld of %ld\n", bad, n);
  return bad == 0 && n >= 1000000 ? 0 : 1;
}
