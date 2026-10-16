/* A stand-in for Wine's ntdll.so, for the plain-Linux test programs only (milestone 4b-1): the guest winemetal.so
 * links ntdll.so for NtSetEvent, which here records the handle; mn_stub_signalled waits for one. */
#include <pthread.h>
#include <stdint.h>
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
