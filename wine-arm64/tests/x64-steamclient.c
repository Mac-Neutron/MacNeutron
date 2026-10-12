// The genuine steamclient64.dll (Wine patch 0036): SteamStub DRM checks the signature of the prefix's
// steamclient64.dll on disk, so the prefix holds Valve's signed one and Wine's loader makes it lsteamclient.dll's in
// memory, as Proton's does: each export lsteamclient.dll also has starts with an x64 jump to lsteamclient.dll's, its
// tier0_s64.dll and vstdlib_s64.dll imports get ntdll stubs, and its DllMain and TLS callbacks never run (check.sh's
// steamclient step reads that off Wine's trace+module lines).
//   x64-steamclient genuine <dll>  Valve's DLL, loaded as a game's steam_api64.dll loads it, under FEX; then a new
//                                  thread (Valve's TLS callback must stay off) and a second load after FreeLibrary
//   x64-steamclient builtin <dll>  lsteamclient.dll copied as steamclient64.dll (today's prefixes): left as it is
// One line per row, `ok <row>` or `FAIL <row>: <why>`, `info` lines, then PASS or FAIL x64-steamclient.
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>
#include <string.h>

static int failed;

static void row(int ok, const char *name, const char *why) {
  if (ok) {
    printf("ok %s\n", name);
    return;
  }
  printf("FAIL %s: %s\n", name, why);
  failed++;
}

// Proton's jump: mov rax, imm64; jmp rax.
static int jumps(const BYTE *p) { return p[0] == 0x48 && p[1] == 0xb8 && p[10] == 0xff && p[11] == 0xe0; }

static int jumps_to(const BYTE *p, const void *target) { return jumps(p) && !memcmp(p + 2, &target, sizeof(target)); }

// What WinVerifyTrust (Authenticode, no revocation, no network) says of the file under this Wine: reported, not gated.
static LONG verify(const WCHAR *path) {
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  WINTRUST_FILE_INFO file = {sizeof(file), path, NULL, NULL};
  WINTRUST_DATA data = {sizeof(data)};
  data.dwUIChoice = WTD_UI_NONE;
  data.fdwRevocationChecks = WTD_REVOKE_NONE;
  data.dwUnionChoice = WTD_CHOICE_FILE;
  data.pFile = &file;
  data.dwStateAction = WTD_STATEACTION_VERIFY;
  data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
  LONG ret = WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &data);
  data.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(INVALID_HANDLE_VALUE, &action, &data);
  return ret;
}

static DWORD WINAPI thread_main(void *arg) { return 7; }

// The genuine DLL's rows; `prefix` names the second load's.
static void check_genuine(HMODULE dll, const WCHAR *want, const char *prefix) {
  WCHAR got[MAX_PATH] = L"";
  char name_buf[64];
  GetModuleFileNameW(dll, got, MAX_PATH);
  BYTE *base = (BYTE *)dll;
  IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
  int marked = !memcmp(base + sizeof(IMAGE_DOS_HEADER), "Wine builtin DLL", 16);
  HMODULE ls = GetModuleHandleW(L"lsteamclient.dll");
  BYTE *create = (BYTE *)GetProcAddress(dll, "CreateInterface");
#define ROW(ok, name, why) (snprintf(name_buf, sizeof(name_buf), "%s%s", prefix, name), row(ok, name_buf, why))
  ROW(!_wcsicmp(got, want) && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && !marked &&
          nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_SECURITY].Size,
      "file", "not Valve's signed x64 file, mapped as it is");
  ROW(ls != NULL, "lsteamclient", "the loader didn't load lsteamclient.dll");
  if (ls) {
    IMAGE_EXPORT_DIRECTORY *exports =
        (IMAGE_EXPORT_DIRECTORY *)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    DWORD *names = (DWORD *)(base + exports->AddressOfNames);
    int matched = 0, patched = 0;
    for (DWORD i = 0; i < exports->NumberOfNames; i++) {
      const char *name = (const char *)(base + names[i]);
      void *target = (void *)GetProcAddress(ls, name);
      if (!target) continue;  // Breakpad_SteamSendMiniDump: lsteamclient.dll has none, as on Proton
      matched++;
      if (jumps_to((BYTE *)GetProcAddress(dll, name), target)) patched++;
      else printf("info not patched: %s\n", name);
    }
    printf("info trampolines %d of %d matched exports (%lu exports)\n", patched, matched, exports->NumberOfNames);
    ROW(matched && patched == matched && create && jumps_to(create, (void *)GetProcAddress(ls, "CreateInterface")),
        "trampolines", "an export lsteamclient.dll also has doesn't jump to it");
    // Through FEX: Valve's x64 bytes, the jump, lsteamclient.dll's export thunk, its ARM64EC code, and back. Valve's
    // own body stays unpatched only where the row above already failed.
    int(__cdecl * set_comment)(const char *) =
        (int(__cdecl *)(const char *))(void *)GetProcAddress(dll, "Breakpad_SteamWriteMiniDumpSetComment");
    ROW(set_comment && set_comment("x64-steamclient") == 1, "call", "a call through a jump didn't return 1");
  }
#undef ROW
}

int wmain(int argc, wchar_t **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);  // a crash keeps what was printed before it
  if (argc != 3 || (wcscmp(argv[1], L"genuine") && wcscmp(argv[1], L"builtin"))) {
    printf("FAIL x64-steamclient: usage: x64-steamclient genuine|builtin <steamclient64.dll>\n");
    return 1;
  }
  int genuine = !wcscmp(argv[1], L"genuine");
  WCHAR want[MAX_PATH], got[MAX_PATH] = L"";
  GetFullPathNameW(argv[2], MAX_PATH, want, NULL);
  if (genuine) printf("info winverifytrust 0x%08lx (0: this Wine trusts Valve's signature)\n", verify(want));

  HMODULE dll = LoadLibraryW(want);
  if (!dll) {
    printf("FAIL x64-steamclient: LoadLibrary failed, error %lu\n", GetLastError());
    return 1;
  }
  if (!genuine) {
    GetModuleFileNameW(dll, got, MAX_PATH);
    int marked = !memcmp((BYTE *)dll + sizeof(IMAGE_DOS_HEADER), "Wine builtin DLL", 16);
    HMODULE ls = GetModuleHandleW(L"lsteamclient.dll");
    BYTE *create = (BYTE *)GetProcAddress(dll, "CreateInterface");
    row(!_wcsicmp(got, want) && marked, "file", "not the builtin copy itself");
    row(!ls && create && !jumps(create), "untouched",
        ls ? "the loader loaded lsteamclient.dll for a builtin" : "the builtin's CreateInterface was patched");
  } else {
    check_genuine(dll, want, "");
    // Valve's TLS callback runs its thread_local initializers on THREAD_ATTACH, against ntdll stubs: it must not run
    HANDLE thread = CreateThread(NULL, 0, thread_main, NULL, 0, NULL);
    DWORD code = 0;
    row(thread && !WaitForSingleObject(thread, 10000) && GetExitCodeThread(thread, &code) && code == 7, "thread",
        "a new thread didn't run to its end");
    // SteamStub loads the DLL, then the game's steam_api64.dll again: the loader's cached lsteamclient.dll path
    FreeLibrary(dll);
    if (!(dll = LoadLibraryW(want))) {
      printf("FAIL x64-steamclient: second LoadLibrary failed, error %lu\n", GetLastError());
      return 1;
    }
    check_genuine(dll, want, "reload ");
  }
  if (!failed) {
    printf("PASS x64-steamclient\n");
    return 0;
  }
  printf("FAIL x64-steamclient: %d rows failed\n", failed);
  return 1;
}
