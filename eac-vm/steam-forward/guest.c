/* The guest's lsteamclient Unix half for Wine (spec §5.9). Installed as Wine's x86_64-unix/lsteamclient.so: every
 * entry of its call table (guest_table.c) forwards to mn-steam-host over vsock, and the registry writes the host side
 * recorded are replayed here, into this prefix, through Wine's own ntdll. HKCU-relative keys go under the calling
 * user's hive; absolute ones (\Registry\Machine\...) as they are. Wine's server creates missing parent keys. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wine/unixlib.h"
#include "client.h"
#include "vsock.h"

static NTSTATUS user_prefix(char *buf, size_t size) {
    DWORD_PTR sid_data[(sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE) / sizeof(DWORD_PTR)];
    ULONG len = sizeof sid_data;
    NTSTATUS status = NtQueryInformationToken(GetCurrentThreadEffectiveToken(), TokenUser, sid_data, len, &len);
    if (status) return status;
    SID *sid = (SID *)((TOKEN_USER *)sid_data)->User.Sid;
    int n = snprintf(buf, size, "\\Registry\\User\\S-%u-%u", sid->Revision,
                     (int)MAKELONG(MAKEWORD(sid->IdentifierAuthority.Value[5], sid->IdentifierAuthority.Value[4]),
                                   MAKEWORD(sid->IdentifierAuthority.Value[3], sid->IdentifierAuthority.Value[2])));
    for (DWORD i = 0; i < sid->SubAuthorityCount && n > 0 && (size_t)n < size; i++)
        n += snprintf(buf + n, size - n, "-%u", (int)sid->SubAuthority[i]);
    return n > 0 && (size_t)n < size ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
}

static void replay(void *ctx, uint32_t type, const char *key, size_t key_len, const char *name, size_t name_len,
                   const void *data, size_t data_len) {
    (void)ctx;
    char path[2048];
    int n;
    if (key_len >= 10 && !strncasecmp(key, "\\Registry\\", 10)) {
        n = snprintf(path, sizeof path, "%.*s", (int)key_len, key);
    } else {
        char user[256];
        if (user_prefix(user, sizeof user)) return;
        n = snprintf(path, sizeof path, "%s\\%.*s", user, (int)key_len, key);
    }
    if (n <= 0 || (size_t)n >= sizeof path) return;
    WCHAR wpath[2048], wname[512];
    DWORD wl = ntdll_umbstowcs(path, n, wpath, sizeof wpath / sizeof *wpath - 1);
    DWORD nl = ntdll_umbstowcs(name, name_len, wname, sizeof wname / sizeof *wname - 1);
    UNICODE_STRING keyU = { wl * sizeof(WCHAR), wl * sizeof(WCHAR), wpath };
    UNICODE_STRING nameU = { nl * sizeof(WCHAR), nl * sizeof(WCHAR), wname };
    OBJECT_ATTRIBUTES attr;
    HANDLE hkey;
    InitializeObjectAttributes(&attr, &keyU, OBJ_CASE_INSENSITIVE, 0, NULL);
    if (NtCreateKey(&hkey, KEY_ALL_ACCESS, &attr, 0, NULL, 0, NULL)) {
        fprintf(stderr, "mn-steam: cannot create registry key %s\n", path);
        return;
    }
    NtSetValueKey(hkey, &nameU, 0, type, data, (ULONG)data_len);
    NtClose(hkey);
}

__attribute__((constructor)) static void mn_steam_init(void) {
    mn_client_init(mn_vsock_connect, replay, NULL);
}
