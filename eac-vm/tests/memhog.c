// memhog <MiB> <hold s>: touches MiB of anonymous memory, holds it, unmaps it, then idles 60 s (for footprint.sh).
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: memhog <MiB> <hold s>\n"); return 2; }
  size_t n = (size_t)atol(argv[1]) << 20;
  char *p = mmap(0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) { perror("mmap"); return 1; }
  for (size_t i = 0; i < n; i += 4096) p[i] = 1;
  printf("touched\n"); fflush(stdout);
  sleep(atoi(argv[2]));
  munmap(p, n);
  printf("freed\n"); fflush(stdout);
  sleep(60);
  return 0;
}
