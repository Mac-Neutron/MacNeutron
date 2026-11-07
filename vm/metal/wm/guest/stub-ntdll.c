/* A stand-in for Wine's ntdll.so, for the plain-Linux test programs only (milestone 4b-1): the guest winemetal.so
 * links ntdll.so for NtSetEvent, which here records the handle; mn_stub_signalled waits for one; NtClose (Direct3D
 * forwarding's events), which closes nothing here; and NtAllocateVirtualMemory, which galloc takes a 32-bit caller's
 * blobs from (the i686 test's): mmap's memory, below 4 GiB in an i686 process. With no NtFreeVirtualMemory a release
 * fails, and the transport maps over the range itself. */
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static uint64_t events[64];
static int nevents;

unsigned int NtSetEvent(void *handle, int *prev_state) {
    if (prev_state) *prev_state = 0;
    pthread_mutex_lock(&lock);
    if (nevents < 64) events[nevents++] = (uint64_t)(uintptr_t)handle;
    pthread_cond_broadcast(&cond);
    pthread_mutex_unlock(&lock);
    return 0;
}

unsigned int NtClose(void *handle) {
    (void)handle;
    return 0;
}

int mn_stub_signalled(uint64_t nt_event, int timeout_ms) {
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += timeout_ms / 1000;
    end.tv_nsec += (long)(timeout_ms % 1000) * 1000000;
    if (end.tv_nsec >= 1000000000) end.tv_sec++, end.tv_nsec -= 1000000000;
    pthread_mutex_lock(&lock);
    int found = 0;
    for (;;) {
        for (int i = 0; i < nevents; i++) found |= events[i] == nt_event;
        if (found || pthread_cond_timedwait(&cond, &lock, &end)) break;
    }
    pthread_mutex_unlock(&lock);
    return found;
}

int32_t NtAllocateVirtualMemory(void *process, void **addr, uintptr_t zero_bits, size_t *size, uint32_t type,
                                uint32_t protect) {
    (void)process, (void)zero_bits, (void)type, (void)protect;
    void *p = mmap(NULL, *size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return (int32_t)0xC0000017; /* STATUS_NO_MEMORY */
    *addr = p;
    return 0;
}
