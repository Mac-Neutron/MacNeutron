// The native build's Win32 handles (DXMT's src/util/win32_native.h, Direct3D forwarding M1): events, semaphores,
// waits with and without a timeout, files under a non-ASCII folder, the clock and modules. Prints "ok <case>" or
// "FAIL <case>" for each, and exits 1 on a failure. Built and run by win32.sh.
#include <win32_native.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>
#include <unistd.h>

void set_from_another_object(HANDLE event);

static int failed;
static void check(const char *name, bool ok) {
    printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) failed = 1;
}

static double ms_between(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER f) {
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / (double)f.QuadPart;
}

int main() {
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);

    // event: an auto-reset event set from another thread wakes an INFINITE wait, then is reset; a manual-reset one
    // stays set until ResetEvent.
    {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::thread t([e] { usleep(20000); SetEvent(e); });
        bool woke = WaitForSingleObject(e, INFINITE) == WAIT_OBJECT_0;
        t.join();
        bool reset = WaitForSingleObject(e, 0) == WAIT_TIMEOUT;
        HANDLE m = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        SetEvent(m);
        bool stays = WaitForSingleObject(m, 0) == WAIT_OBJECT_0 && WaitForSingleObject(m, 0) == WAIT_OBJECT_0;
        ResetEvent(m);
        bool unset = WaitForSingleObject(m, 0) == WAIT_TIMEOUT;
        check("event", e && m && woke && reset && stays && unset && CloseHandle(e) && CloseHandle(m));
    }

    // timeout: a wait on an unset event returns WAIT_TIMEOUT after about its 50 ms.
    {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        QueryPerformanceCounter(&a);
        DWORD r = WaitForSingleObject(e, 50);
        QueryPerformanceCounter(&b);
        double took = ms_between(a, b, f);
        check("timeout", r == WAIT_TIMEOUT && took >= 40 && took <= 200);
        CloseHandle(e);
    }

    // semaphore: count 1, maximum 2.
    {
        HANDLE s = CreateSemaphore(nullptr, 1, 2, nullptr);
        bool first = WaitForSingleObject(s, 0) == WAIT_OBJECT_0, second = WaitForSingleObject(s, 0) == WAIT_TIMEOUT;
        LONG prev = -1;
        bool over = !ReleaseSemaphore(s, 3, &prev);
        bool one = ReleaseSemaphore(s, 1, &prev) && prev == 0;
        HANDLE d = nullptr;
        bool dup = DuplicateHandle(GetCurrentProcess(), s, GetCurrentProcess(), &d, 0, FALSE, 2 /* SAME_ACCESS */) &&
                   CloseHandle(s) && WaitForSingleObject(d, 0) == WAIT_OBJECT_0; // the duplicate outlives the original
        check("semaphore", s && first && second && over && one && dup && CloseHandle(d));
    }

    // cross: the other translation unit's SetEvent wakes this one's wait.
    {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::thread t([e] { usleep(10000); set_from_another_object(e); });
        bool woke = WaitForSingleObject(e, 2000) == WAIT_OBJECT_0;
        t.join();
        check("cross", woke && CloseHandle(e));
    }

    // file: create, write, size, read back, truncate, replace, attributes, delete, under "caché-é".
    {
        char tmpl[] = "/tmp/win32-test-XXXXXX";
        std::string base = mkdtemp(tmpl) ? tmpl : "";
        std::wstring dir = std::wstring(base.begin(), base.end()) + L"/caché-é";
        std::wstring path = dir + L"/a.bin", other = dir + L"/b.bin";
        bool made = CreateDirectoryW(dir.c_str(), nullptr);
        bool again = !CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() == ERROR_ALREADY_EXISTS;
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD n = 0;
        LARGE_INTEGER size = {};
        char back[5] = {};
        bool wrote = h != INVALID_HANDLE_VALUE && WriteFile(h, "hello", 5, &n, nullptr) && n == 5;
        bool sized = GetFileSizeEx(h, &size) && size.QuadPart == 5;
        bool read = SetFilePointer(h, 0, nullptr, FILE_BEGIN) == 0 && ReadFile(h, back, 5, &n, nullptr) && n == 5 &&
                    !memcmp(back, "hello", 5);
        LARGE_INTEGER two = {}, at = {};
        two.QuadPart = 2;
        bool cut = SetFilePointerEx(h, two, &at, FILE_BEGIN) && at.QuadPart == 2 && SetEndOfFile(h) &&
                   GetFileSizeEx(h, &size) && size.QuadPart == 2;
        CloseHandle(h);
        HANDLE o = CreateFileW(other.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CloseHandle(o);
        bool moved = MoveFileExW(path.c_str(), other.c_str(), MOVEFILE_REPLACE_EXISTING) &&
                     GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES &&
                     GetFileAttributesW(other.c_str()) != INVALID_FILE_ATTRIBUTES;
        bool gone = DeleteFileW(other.c_str()) && GetFileAttributesW(other.c_str()) == INVALID_FILE_ATTRIBUTES;
        std::string utf8 = base + "/cach\xc3\xa9-\xc3\xa9";
        bool named = access(utf8.c_str(), F_OK) == 0; // the folder's name on disk is the path's UTF-8
        check("file", made && again && wrote && sized && read && cut && moved && gone && named);
        rmdir(utf8.c_str());
        rmdir(base.c_str());
    }

    // clock: nanoseconds, and Sleep(10) about 10 ms of them.
    {
        QueryPerformanceCounter(&a);
        Sleep(10);
        QueryPerformanceCounter(&b);
        double took = ms_between(a, b, f);
        check("clock", f.QuadPart == 1000000000 && took >= 8 && took <= 50 && GetTickCount64() > 0);
    }

    // thread: another thread's name by its id (DXMT_STATS names every thread it saw from the one that reports), and
    // the calling thread's through its pseudo-handle.
    {
        DWORD id = 0;
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr), done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::thread t([&] {
            pthread_setname_np("mn-named");
            id = GetCurrentThreadId();
            SetEvent(ready);
            WaitForSingleObject(done, INFINITE);
        });
        WaitForSingleObject(ready, INFINITE);
        LPWSTR name = nullptr, mine = nullptr;
        HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, id);
        bool other = th && SUCCEEDED(GetThreadDescription(th, &name)) && name && std::wstring(name) == L"mn-named";
        LocalFree(name);
        CloseHandle(th);
        SetEvent(done);
        t.join();
        pthread_setname_np("mn-main");
        bool self = SUCCEEDED(GetThreadDescription(GetCurrentThread(), &mine)) && mine && std::wstring(mine) == L"mn-main";
        LocalFree(mine);
        check("thread", id && other && self && !OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0));
        CloseHandle(ready);
        CloseHandle(done);
    }

    // winpath: DXMT builds its dump and recording paths the Windows way, a drive and backslashes ("Z:" + a native
    // folder, then "\\name"): natively they are the same POSIX path.
    {
        char tmpl[] = "/tmp/win32-winpath-XXXXXX";
        std::string base = mkdtemp(tmpl) ? tmpl : "";
        std::wstring w = L"Z:" + std::wstring(base.begin(), base.end());
        bool made = CreateDirectoryW((w + L"\\sub").c_str(), nullptr);
        HANDLE h = CreateFileW((w + L"\\sub\\f.bin").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        bool opened = h != INVALID_HANDLE_VALUE;
        CloseHandle(h);
        bool there = access((base + "/sub/f.bin").c_str(), F_OK) == 0;
        check("winpath", made && opened && there);
        unlink((base + "/sub/f.bin").c_str());
        rmdir((base + "/sub").c_str());
        rmdir(base.c_str());
    }

    // closed: an event a fence holds (the listener's reference) and the program closes before the fence fires is
    // still the event when it is set, then freed (ASan: no use after free).
    {
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        mn_win32_ref(e);
        bool closed = CloseHandle(e);
        bool set = mn_win32_set_event(e) == 1;
        mn_win32_unref(e);
        check("closed", closed && set);
    }

    // module: dlopen and dlsym behind LoadLibraryA and GetProcAddress.
    {
        HMODULE m = LoadLibraryA("/usr/lib/libSystem.B.dylib");
        check("module", m && GetProcAddress(m, "getpid"));
    }
    return failed;
}
