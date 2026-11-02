// Native winemetal's exports for the Rust front ends (DXMTRS step 5c, Task 1): the thread-local not-implemented flag
// (wmt_native_not_implemented, MTLD3DNotImplemented) and the Win32 handle reference count (wmt_native_handle_count).
// Prints "ok   <case>" or "FAIL <case>" for each, and exits 1 on a failure. Built and run by wmt-exports.sh.
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

void wmt_native_not_implemented(void);
bool MTLD3DNotImplemented(void);
uint32_t wmt_native_handle_count(void *handle);
void *wmt_native_create_event(int manual_reset, int initial_state);
void wmt_native_ref(void *event);
void wmt_native_unref(void *event);
int wmt_native_close_handle(void *handle);

static int failed;
static void check(const char *name, bool ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) failed = 1;
}

struct thread_state {
    bool start_clear;
    bool after_set;
    bool after_clear;
};

static void *worker_thread(void *arg) {
    struct thread_state *st = (struct thread_state *)arg;
    st->start_clear = !MTLD3DNotImplemented();
    wmt_native_not_implemented();
    st->after_set = MTLD3DNotImplemented();
    st->after_clear = !MTLD3DNotImplemented();
    // Leave thread B's flag set on exit to verify thread A's flag stays false.
    wmt_native_not_implemented();
    return NULL;
}

int main(void) {
    // not_implemented_flag_reads_and_clears: starts false, wmt_native_not_implemented sets it, first
    // MTLD3DNotImplemented reads true and clears it, second reads false; thread-local across pthreads.
    {
        bool start = !MTLD3DNotImplemented();
        wmt_native_not_implemented();
        bool first = MTLD3DNotImplemented();
        bool second = !MTLD3DNotImplemented();
        struct thread_state st = {false, false, false};
        pthread_t th;
        int rc = pthread_create(&th, NULL, worker_thread, &st);
        if (rc == 0) pthread_join(th, NULL);
        bool isolated = !MTLD3DNotImplemented();
        check("not_implemented_flag_reads_and_clears",
              start && first && second && rc == 0 && st.start_clear && st.after_set && st.after_clear && isolated);
    }

    // handle_count_follows_refs: NULL and non-handle pointers report 0; a live event reports 1, 2 after wmt_native_ref,
    // 1 after wmt_native_unref.
    {
        uint32_t dummy = 0;
        bool null_zero = wmt_native_handle_count(NULL) == 0 && wmt_native_handle_count(&dummy) == 0;
        void *h = wmt_native_create_event(1, 0);
        bool init_one = h != NULL && wmt_native_handle_count(h) == 1;
        wmt_native_ref(h);
        bool ref_two = wmt_native_handle_count(h) == 2;
        wmt_native_unref(h);
        bool unref_one = wmt_native_handle_count(h) == 1;
        bool closed = wmt_native_close_handle(h) != 0;
        check("handle_count_follows_refs", null_zero && init_one && ref_two && unref_one && closed);
    }

    return failed;
}
