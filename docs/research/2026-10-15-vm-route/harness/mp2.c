// Message passing: the writer increments data then flag; the reader loads flag then data. data < flag (the reader saw
// the flag's store before the data's) is forbidden under TSO and allowed on Arm's weak model.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#define N 200000000L
static volatile long data __attribute__((aligned(128)));
static volatile long flag __attribute__((aligned(128)));
static atomic_int stop;
static void *writer(void *a) { for (long i = 0; i < N; i++) { data = i + 1; flag = i + 1; } atomic_store(&stop, 1); return 0; }
int main(void) {
  pthread_t t; pthread_create(&t, 0, writer, 0);
  long bad = 0, n = 0;
  while (!atomic_load_explicit(&stop, memory_order_relaxed)) { long f = flag; long d = data; if (d < f) bad++; n++; }
  pthread_join(t, 0);
  printf("violations %ld of %ld\n", bad, n);
}
