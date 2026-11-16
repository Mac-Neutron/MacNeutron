// winemetal.dylib's Win32 exports (wmt_native_*, MTLD3DNotImplemented) against the header's inline functions
// (DXMT's src/util/win32_native.h) on the same handles: what one side makes, the other sets, waits on, duplicates
// and closes. The dylib's calls and the header's are two implementations of one handle layout; this is where they
// meet. Prints "ok <case>" or "FAIL <case>" for each, and exits 1 on a failure. Built and run by win32-cross.sh.
#include <win32_native.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>

extern "C" {
void *wmt_native_create_event(int32_t manual, int32_t initial);
void *wmt_native_create_semaphore(int32_t initial, int32_t max);
int32_t wmt_native_release_semaphore(void *handle, int32_t count, int32_t *previous);
uint32_t wmt_native_wait_for_single_object(void *handle, uint32_t milliseconds);
int32_t wmt_native_duplicate_handle(void *source, void **target, uint32_t options);
void *wmt_native_open_thread(uint32_t thread_id);
int32_t wmt_native_close_handle(void *handle);
int wmt_native_set_event(void *handle);
void wmt_native_ref(void *handle);
int wmt_native_unref(void *handle);
void wmt_native_not_implemented(void);
bool MTLD3DNotImplemented(void);
uint32_t wmt_native_handle_count(void *handle);
}

static int failed;
static void check(const char *name, bool ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) failed = 1;
}

static int refs_of(void *h) { return __atomic_load_n(&((mn_win32_handle *)h)->refs, __ATOMIC_RELAXED); }

static double ms_between(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER f) {
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

int main() {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);

    // dylib-event: an auto-reset event the dylib made, set by the header from another thread, wakes the header's
    // INFINITE wait and is consumed; the header closes it.
    {
        void *e = wmt_native_create_event(0, 0);
        bool layout = e && ((mn_win32_handle *)e)->kind == MN_WIN32_EVENT && refs_of(e) == 1 &&
                      ((mn_win32_handle *)e)->max == 1 && ((mn_win32_handle *)e)->count == 0;
        std::thread t([e] { usleep(20000); SetEvent(e); });
        bool woke = WaitForSingleObject(e, INFINITE) == WAIT_OBJECT_0;
        t.join();
        bool consumed = WaitForSingleObject(e, 0) == WAIT_TIMEOUT;
        void *set = wmt_native_create_event(0, 7);
        bool initial = set && ((mn_win32_handle *)set)->count == 1 && WaitForSingleObject(set, 0) == WAIT_OBJECT_0;
        check("dylib-event", layout && woke && consumed && initial && CloseHandle(e) && CloseHandle(set));
    }

    // header-event: a manual-reset event the header made, set and waited on by the dylib from another thread; it
    // stays set until the header resets it; the dylib closes it.
    {
        HANDLE m = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::thread t([m] { usleep(20000); wmt_native_set_event(m); });
        bool woke = wmt_native_wait_for_single_object(m, INFINITE) == WAIT_OBJECT_0;
        t.join();
        bool stays = wmt_native_wait_for_single_object(m, 0) == WAIT_OBJECT_0 &&
                     wmt_native_wait_for_single_object(m, 0) == WAIT_OBJECT_0;
        ResetEvent(m);
        bool unset = wmt_native_wait_for_single_object(m, 0) == WAIT_TIMEOUT;
        check("header-event", m && woke && stays && unset && wmt_native_close_handle(m) == 1);
    }

    // timeout: the dylib's timed wait on an unset event returns WAIT_TIMEOUT after about its 50 ms, and so does the
    // header's on an event the dylib made.
    {
        void *e = wmt_native_create_event(0, 0);
        QueryPerformanceCounter(&a);
        uint32_t r = wmt_native_wait_for_single_object(e, 50);
        QueryPerformanceCounter(&b);
        double took = ms_between(a, b, f);
        QueryPerformanceCounter(&a);
        DWORD r2 = WaitForSingleObject(e, 50);
        QueryPerformanceCounter(&b);
        double took2 = ms_between(a, b, f);
        check("timeout", r == WAIT_TIMEOUT && took >= 40 && took <= 200 && r2 == WAIT_TIMEOUT && took2 >= 40 &&
                             took2 <= 200 && wmt_native_close_handle(e) == 1);
    }

    // semaphore: the dylib's, count 1 of 2, taken by the header and released by both; a release past the maximum is
    // refused and leaves `previous` alone. Then the header's, waited on by the dylib.
    {
        void *s = wmt_native_create_semaphore(1, 2);
        bool first = WaitForSingleObject(s, 0) == WAIT_OBJECT_0, second = WaitForSingleObject(s, 0) == WAIT_TIMEOUT;
        int32_t prev = -1;
        LONG hprev = -1;
        bool up = wmt_native_release_semaphore(s, 1, &prev) == 1 && prev == 0;
        bool up2 = ReleaseSemaphore(s, 1, &hprev) && hprev == 1;
        prev = -5;
        bool full = wmt_native_release_semaphore(s, 1, &prev) == 0 && prev == -5;
        bool bad_n = wmt_native_release_semaphore(s, 0, &prev) == 0 && wmt_native_release_semaphore(s, -1, &prev) == 0;
        bool no_prev = WaitForSingleObject(s, 0) == WAIT_OBJECT_0 && wmt_native_release_semaphore(s, 1, nullptr) == 1;
        HANDLE hs = CreateSemaphore(nullptr, 0, 1, nullptr);
        bool empty = wmt_native_wait_for_single_object(hs, 0) == WAIT_TIMEOUT;
        std::thread t([hs] { usleep(20000); ReleaseSemaphore(hs, 1, nullptr); });
        bool got = wmt_native_wait_for_single_object(hs, 2000) == WAIT_OBJECT_0;
        t.join();
        bool taken = wmt_native_wait_for_single_object(hs, 0) == WAIT_TIMEOUT;
        check("semaphore", s && first && second && up && up2 && full && bad_n && no_prev && hs && empty && got &&
                               taken && CloseHandle(s) && wmt_native_close_handle(hs) == 1);
    }

    // semaphore-refused: the limits CreateSemaphoreW refuses, and a release on an event.
    {
        void *e = wmt_native_create_event(0, 0);
        int32_t prev = -5;
        check("semaphore-refused", !wmt_native_create_semaphore(0, 0) && !wmt_native_create_semaphore(-1, 1) &&
                                       !wmt_native_create_semaphore(2, 1) && !wmt_native_create_semaphore(0, -1) &&
                                       wmt_native_release_semaphore(e, 1, &prev) == 0 && prev == -5 &&
                                       wmt_native_close_handle(e) == 1);
    }

    // duplicate: an event's duplicate is another reference to the same object, made by one side and closed by the
    // other; DUPLICATE_CLOSE_SOURCE gives the reference back.
    {
        void *e = wmt_native_create_event(1, 0), *d = nullptr;
        HANDLE d2 = nullptr;
        bool dup = wmt_native_duplicate_handle(e, &d, 0) == 1 && d == e && refs_of(e) == 2 &&
                   wmt_native_handle_count(e) == 2;
        bool closed = CloseHandle(d) && refs_of(e) == 1;
        bool hdup = DuplicateHandle(GetCurrentProcess(), e, GetCurrentProcess(), &d2, 0, FALSE, 0) && d2 == e &&
                    wmt_native_handle_count(e) == 2;
        bool hclosed = wmt_native_close_handle(d2) == 1 && refs_of(e) == 1;
        void *moved = nullptr;
        bool move = wmt_native_duplicate_handle(e, &moved, DUPLICATE_CLOSE_SOURCE) == 1 && moved == e && refs_of(e) == 1;
        check("duplicate", e && dup && closed && hdup && hclosed && move && CloseHandle(moved));
    }

    // duplicate-refused: no target, no source, the pseudo-handles and a relay.
    {
        void *e = wmt_native_create_event(0, 0), *d = (void *)0x11;
        HANDLE relay = mn_win32_relay_event([](void *) {}, nullptr, nullptr);
        bool refused = wmt_native_duplicate_handle(e, nullptr, 0) == 0 && wmt_native_duplicate_handle(nullptr, &d, 0) == 0 &&
                       wmt_native_duplicate_handle((void *)(intptr_t)-1, &d, 0) == 0 &&
                       wmt_native_duplicate_handle((void *)(intptr_t)-2, &d, 0) == 0 &&
                       wmt_native_duplicate_handle(relay, &d, 0) == 0;
        check("duplicate-refused", refused && d == (void *)0x11 && refs_of(e) == 1 && CloseHandle(e) && CloseHandle(relay));
    }

    // file: a file handle the header opened is duplicated by the dylib into a handle of its own descriptor, which
    // outlives the first; the dylib cannot wait on it; each side closes one.
    {
        char tmpl[] = "/tmp/win32-cross-XXXXXX";
        std::string base = mkdtemp(tmpl) ? tmpl : "";
        std::wstring w(base.begin(), base.end());
        HANDLE h = CreateFileW((w + L"/f.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        void *d = nullptr;
        bool dup = h != INVALID_HANDLE_VALUE && wmt_native_duplicate_handle(h, &d, 0) == 1 && d && d != h &&
                   ((mn_win32_handle *)d)->kind == MN_WIN32_FILE && refs_of(d) == 1 && refs_of(h) == 1 &&
                   ((mn_win32_handle *)d)->fd >= 0 && ((mn_win32_handle *)d)->fd != ((mn_win32_handle *)h)->fd;
        bool no_wait = dup && wmt_native_wait_for_single_object(d, 0) == WAIT_FAILED;
        bool first_closed = dup && wmt_native_close_handle(h) == 1;
        DWORD n = 0;
        bool wrote = dup && WriteFile(d, "ab", 2, &n, nullptr) && n == 2;
        int fd = dup ? ((mn_win32_handle *)d)->fd : -1;
        bool second_closed = dup && CloseHandle(d) && fcntl(fd, F_GETFD) == -1;
        check("file", dup && no_wait && first_closed && wrote && second_closed);
        unlink((base + "/f.bin").c_str());
        rmdir(base.c_str());
    }

    // relay: the dylib's set calls the relay's function on the setter's thread; the dylib cannot wait on it; the
    // dylib's drop of the last reference calls its end once.
    {
        static std::atomic<int> calls{0}, ends{0};
        static int token = 7;
        HANDLE r = mn_win32_relay_event([](void *ctx) { calls += ctx == &token; },
                                        [](void *ctx) { ends += ctx == &token; }, &token);
        bool set = wmt_native_set_event(r) == 1 && calls == 1;
        bool no_wait = wmt_native_wait_for_single_object(r, 0) == WAIT_FAILED;
        wmt_native_ref(r);
        bool held = wmt_native_handle_count(r) == 2 && CloseHandle(r) && ends == 0;
        bool dropped = wmt_native_unref(r) == 1 && ends == 1 && calls == 1;
        HANDLE plain = mn_win32_relay_event([](void *) {}, nullptr, nullptr);
        check("relay", r && set && no_wait && held && dropped && wmt_native_close_handle(plain) == 1);
    }

    // closed: an event a listener holds through the dylib and the program closes first is still the event when the
    // dylib sets it, then freed by the dylib's unref (ASan: no use after free).
    {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        wmt_native_ref(e);
        bool closed = CloseHandle(e) && wmt_native_handle_count(e) == 1;
        bool set = wmt_native_set_event(e) == 1 && WaitForSingleObject(e, 0) == WAIT_OBJECT_0;
        check("closed", closed && set && wmt_native_unref(e) == 1);
    }

    // thread: this thread opened by its id is a thread handle, which nothing waits on and either side closes; id 0
    // and an id no thread has are refused.
    {
        DWORD tid = GetCurrentThreadId();
        void *t = wmt_native_open_thread(tid), *d = nullptr;
        bool opened = t && ((mn_win32_handle *)t)->kind == MN_WIN32_THREAD && ((mn_win32_handle *)t)->tid == tid &&
                      refs_of(t) == 1;
        bool no_wait = opened && wmt_native_wait_for_single_object(t, 0) == WAIT_FAILED &&
                       wmt_native_set_event(t) == 0;
        bool dup = opened && wmt_native_duplicate_handle(t, &d, 0) == 1 && d == t && refs_of(t) == 2;
        HANDLE ht = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
        check("thread", opened && no_wait && dup && CloseHandle(d) && CloseHandle(t) && ht &&
                            wmt_native_close_handle(ht) == 1 && !wmt_native_open_thread(0) &&
                            !wmt_native_open_thread(0x7ffffff1));
    }

    // not-a-handle: null, the pseudo-handles and memory that is no handle. CloseHandle answers TRUE for the two
    // pseudo-handles, as Windows does.
    {
        int stack = 0;
        int32_t prev = -5;
        void *d = (void *)0x11;
        bool ok = true;
        for (void *h : {(void *)nullptr, (void *)(intptr_t)-1, (void *)(intptr_t)-2, (void *)&stack}) {
            bool pseudo = h == (void *)(intptr_t)-1 || h == (void *)(intptr_t)-2;
            wmt_native_ref(h);
            ok = ok && wmt_native_set_event(h) == 0 && wmt_native_unref(h) == 0 && wmt_native_handle_count(h) == 0 &&
                 wmt_native_wait_for_single_object(h, 0) == WAIT_FAILED &&
                 wmt_native_release_semaphore(h, 1, &prev) == 0 && wmt_native_close_handle(h) == (pseudo ? 1 : 0);
            if (h != (void *)&stack) ok = ok && wmt_native_duplicate_handle(h, &d, 0) == 0;
        }
        check("not-a-handle", ok && prev == -5 && d == (void *)0x11 && stack == 0);
    }

    // count: the dylib's count is the header's reference count, whichever side moves it.
    {
        void *e = wmt_native_create_event(0, 0);
        mn_win32_ref(e);
        bool two = wmt_native_handle_count(e) == 2 && refs_of(e) == 2;
        wmt_native_ref(e);
        bool three = wmt_native_handle_count(e) == 3 && refs_of(e) == 3;
        bool back = wmt_native_unref(e) == 1 && mn_win32_unref(e) == 1 && wmt_native_handle_count(e) == 1;
        check("count", two && three && back && CloseHandle(e));
    }

    // not-implemented: a flag of the calling thread, false until set, read once.
    {
        bool clear = !MTLD3DNotImplemented();
        wmt_native_not_implemented();
        bool other = true;
        std::thread t([&other] { other = MTLD3DNotImplemented(); });
        t.join();
        bool mine = MTLD3DNotImplemented();
        check("not-implemented", clear && !other && mine && !MTLD3DNotImplemented());
    }
    return failed;
}
