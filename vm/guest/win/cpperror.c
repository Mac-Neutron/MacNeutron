/* std::_Throw_Cpp_error, msvcp140's way to throw a std::system_error for a failed std::thread or std::unique_lock call
 * (MSVC STL thread0.cpp): Age of Empires II DE's main thread called it while starting its UI thread, and Wine's stub
 * ended the game. Usage: cpperror.exe <code> (0-6, the STL's _BUSY .. _RESOURCE_UNAVAILABLE_TRY_AGAIN). Calls it once;
 * a vectored handler reports the first exception: a C++ exception (0xe06d7363) whose most derived type is
 * std::system_error, carrying the error's message. Prints "code N: exception X type T what W" and RESULT: PASS/FAIL. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int code;

static LONG CALLBACK handler(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *rec = ep->ExceptionRecord;
    const char *type = "-", *what = "-";

    if (rec->ExceptionCode == 0xe06d7363 && rec->NumberParameters >= 4)
    {
        /* x64: ExceptionInformation[2] is the ThrowInfo, its fields RVAs from ExceptionInformation[3]; the first
         * catchable type is the most derived one, its TypeDescriptor's name after two pointers */
        const char *base = (const char *)rec->ExceptionInformation[3];
        const int *info = (const int *)rec->ExceptionInformation[2];
        const int *types = (const int *)(base + info[3]);
        const int *catchable = (const int *)(base + types[1]);
        type = base + catchable[1] + 2 * sizeof(void *);
        what = *(const char **)(rec->ExceptionInformation[1] + sizeof(void *)); /* exception: vtable, then its message */
    }
    printf("code %d: exception %#lx type %s what %s\n", code, rec->ExceptionCode, type, what ? what : "(null)");
    printf("RESULT: %s\n", rec->ExceptionCode == 0xe06d7363 && !strcmp(type, ".?AVsystem_error@std@@") && what && *what
           ? "PASS" : "FAIL");
    fflush(stdout);
    ExitProcess(0);
}

int main(int argc, char **argv)
{
    void (__cdecl *throw_cpp_error)(int);
    HMODULE msvcp = LoadLibraryA("msvcp140.dll");

    code = argc > 1 ? atoi(argv[1]) : 0;
    throw_cpp_error = msvcp ? (void *)GetProcAddress(msvcp, "?_Throw_Cpp_error@std@@YAXH@Z") : NULL;
    if (!throw_cpp_error)
    {
        printf("code %d: no msvcp140 _Throw_Cpp_error\nRESULT: FAIL\n", code);
        return 1;
    }
    AddVectoredExceptionHandler(1, handler);
    throw_cpp_error(code);
    printf("code %d: returned\nRESULT: FAIL\n", code);
    return 1;
}
