// The few Win32 calls x64-bench.cpp makes, on POSIX, so the same kernels run as an x86-64 Linux program under FEX.
#pragma once
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
typedef union { long long QuadPart; } LARGE_INTEGER;
typedef unsigned long DWORD;
typedef int32_t LONG;
typedef void *HANDLE;
#define WINAPI
#define INFINITE 0xffffffff
#define TRUE 1
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 4
static inline int QueryPerformanceCounter(LARGE_INTEGER *t) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); t->QuadPart = ts.tv_sec * 1000000000LL + ts.tv_nsec; return 1;
}
static inline int QueryPerformanceFrequency(LARGE_INTEGER *f) { f->QuadPart = 1000000000LL; return 1; }
static inline void ExitProcess(unsigned c) { exit(c); }
static inline unsigned long GetLastError(void) { return errno; }
static inline void OutputDebugStringA(const char *) {}
static inline void Sleep(unsigned) { sched_yield(); }
static inline LONG InterlockedIncrement(volatile LONG *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
// VirtualFree(p, 0, MEM_RELEASE) needs the size back: keep it in front of the mapping.
static inline void *VirtualAlloc(void *, size_t n, unsigned, unsigned) {
  char *p = (char *)mmap(NULL, n + 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) return NULL;
  *(size_t *)p = n + 65536; return p + 65536;
}
static inline int VirtualFree(void *p, size_t, unsigned) { char *b = (char *)p - 65536; return munmap(b, *(size_t *)b) == 0; }
typedef DWORD (*LPTHREAD_START_ROUTINE)(void *);
struct compat_thread { pthread_t t; LPTHREAD_START_ROUTINE fn; void *arg; };
static void *compat_start(void *a) { struct compat_thread *c = (struct compat_thread *)a; c->fn(c->arg); return NULL; }
static inline HANDLE CreateThread(void *, size_t, LPTHREAD_START_ROUTINE fn, void *arg, DWORD, DWORD *) {
  struct compat_thread *c = (struct compat_thread *)malloc(sizeof *c); c->fn = fn; c->arg = arg;
  if (pthread_create(&c->t, NULL, compat_start, c)) { free(c); return NULL; }
  return c;
}
static inline DWORD WaitForMultipleObjects(DWORD n, HANDLE *h, int, DWORD) {
  for (DWORD i = 0; i < n; i++) pthread_join(((struct compat_thread *)h[i])->t, NULL);
  return 0;
}
static inline int CloseHandle(HANDLE h) { free(h); return 1; }
