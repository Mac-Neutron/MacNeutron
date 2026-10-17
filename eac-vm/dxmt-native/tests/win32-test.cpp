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
        bool cut = SetFilePointer(h, 2, nullptr, FILE_BEGIN) == 2 && SetEndOfFile(h) && GetFileSizeEx(h, &size) &&
                   size.QuadPart == 2;
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

    // module: dlopen and dlsym behind LoadLibraryA and GetProcAddress.
    {
        HMODULE m = LoadLibraryA("/usr/lib/libSystem.B.dylib");
        check("module", m && GetProcAddress(m, "getpid"));
    }
    return failed;
}
