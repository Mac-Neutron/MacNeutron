/* The 11 Wine symbols MacNeutron's macOS lsteamclient Unix half imports from ntdll.so, for running it in mn-steam-host
 * outside Wine (spec §5.9). Built as ntdll.so next to a copy of lsteamclient.so, which finds it through
 * `@loader_path/`.
 * - Paths go through one mapping: a host path under the shared library folder becomes the guest's path in Wine's DOS
 *   form (Z: is the guest's /), and back; anything else is refused, so no Mac path reaches the guest.
 * - Registry writes are recorded into the calling thread's log, which mn-steam-host sends to the guest to replay in its
 *   own prefix. Keys under the user's hive become HKCU-relative.
 * - Debug output goes to stderr when MN_STEAM_DEBUG=1. */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "wine/debug.h"
#include "wine/unixlib.h"
#include "marshal.h"
#include "host_wine.h"

/* ---- configuration from mn-steam-host ---- */

static char host_root[4096], guest_root[4096];
static __thread struct mn_reg_log *thread_log;

void mn_stub_set_map(const char *host, const char *guest) {
    snprintf(host_root, sizeof host_root, "%s", host);
    snprintf(guest_root, sizeof guest_root, "%s", guest);
}
void mn_stub_set_reg_log(struct mn_reg_log *log) { thread_log = log; }

/* ---- strings ---- */

DWORD ntdll_umbstowcs(const char *src, DWORD srclen, WCHAR *dst, DWORD dstlen) {
    DWORD n = 0;
    const unsigned char *s = (const unsigned char *)src, *end = s + srclen;
    while (s < end) {
        unsigned int c = *s++, extra = 0;
        if (c >= 0xf0 && c < 0xf8) { c &= 7; extra = 3; }
        else if (c >= 0xe0) { c &= 15; extra = 2; }
        else if (c >= 0xc0) { c &= 31; extra = 1; }
        else if (c >= 0x80) { c = 0xfffd; }
        while (extra--) {
            if (s >= end || (*s & 0xc0) != 0x80) { c = 0xfffd; break; }
            c = (c << 6) | (*s++ & 0x3f);
        }
        if (c >= 0x10000) {
            if (n + 2 > dstlen) break;
            c -= 0x10000;
            dst[n++] = (WCHAR)(0xd800 | (c >> 10));
            dst[n++] = (WCHAR)(0xdc00 | (c & 0x3ff));
        } else {
            if (n + 1 > dstlen) break;
            dst[n++] = (WCHAR)c;
        }
    }
    return n;
}

int ntdll_wcstoumbs(const WCHAR *src, DWORD srclen, char *dst, DWORD dstlen, BOOL strict) {
    DWORD n = 0;
    for (DWORD i = 0; i < srclen; i++) {
        unsigned int c = src[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < srclen && src[i + 1] >= 0xdc00 && src[i + 1] < 0xe000)
            c = 0x10000 + ((c - 0xd800) << 10) + (src[++i] - 0xdc00);
        else if (c >= 0xd800 && c < 0xe000) { if (strict) return -1; c = 0xfffd; }
        unsigned char b[4]; int k;
        if (c < 0x80) { b[0] = (unsigned char)c; k = 1; }
        else if (c < 0x800) { b[0] = 0xc0 | (c >> 6); b[1] = 0x80 | (c & 0x3f); k = 2; }
        else if (c < 0x10000) { b[0] = 0xe0 | (c >> 12); b[1] = 0x80 | ((c >> 6) & 0x3f); b[2] = 0x80 | (c & 0x3f); k = 3; }
        else { b[0] = 0xf0 | (c >> 18); b[1] = 0x80 | ((c >> 12) & 0x3f); b[2] = 0x80 | ((c >> 6) & 0x3f); b[3] = 0x80 | (c & 0x3f); k = 4; }
        if (n + k > dstlen) break;
        memcpy(dst + n, b, k); n += k;
    }
    return (int)n;
}

static size_t wlen(const WCHAR *s) { size_t n = 0; while (s[n]) n++; return n; }

/* ---- paths ---- */

NTSTATUS ntdll_get_dos_file_name(const char *unix_name, WCHAR **dos, UINT disposition) {
    (void)disposition;
    size_t hl = strlen(host_root);
    *dos = NULL;
    if (!hl || strncmp(unix_name, host_root, hl) || (unix_name[hl] && unix_name[hl] != '/'))
        return STATUS_OBJECT_PATH_NOT_FOUND;
    char guest[8192];
    int len = snprintf(guest, sizeof guest, "Z:%s%s", guest_root, unix_name + hl);
    if (len <= 0 || (size_t)len >= sizeof guest) return STATUS_NAME_TOO_LONG;
    for (char *p = guest; *p; p++) if (*p == '/') *p = '\\';
    WCHAR *w = malloc((len + 1) * sizeof(WCHAR));
    if (!w) return STATUS_NO_MEMORY;
    DWORD n = ntdll_umbstowcs(guest, len, w, len);
    w[n] = 0;
    *dos = w;
    return STATUS_SUCCESS;
}

NTSTATUS ntdll_get_unix_file_name(const WCHAR *dos, char **unix_name, UINT disposition) {
    (void)disposition;
    char path[8192];
    size_t gl = strlen(guest_root);
    *unix_name = NULL;
    int n = ntdll_wcstoumbs(dos, wlen(dos), path, sizeof path - 1, FALSE);
    if (n < 0) return STATUS_OBJECT_NAME_INVALID;
    path[n] = 0;
    if (!strncmp(path, "\\??\\", 4)) memmove(path, path + 4, strlen(path + 4) + 1);
    if ((path[0] != 'Z' && path[0] != 'z') || path[1] != ':') return STATUS_OBJECT_PATH_NOT_FOUND;
    for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
    const char *g = path + 2;
    if (!gl || strncmp(g, guest_root, gl) || (g[gl] && g[gl] != '/')) return STATUS_OBJECT_PATH_NOT_FOUND;
    size_t need = strlen(host_root) + strlen(g + gl) + 1;
    char *out = malloc(need);
    if (!out) return STATUS_NO_MEMORY;
    snprintf(out, need, "%s%s", host_root, g + gl);
    *unix_name = out;
    return STATUS_SUCCESS;
}

/* ---- registry: recorded for the guest ---- */

#define MAX_KEYS 64
static pthread_mutex_t keys_lock = PTHREAD_MUTEX_INITIALIZER;
static char *keys[MAX_KEYS];  /* handle i+1 -> key path (UTF-8) */

/* Wine's default user, S-1-5-21-0-0-0-1000: lsteamclient builds \Registry\User\<sid>\... from it. */
static const char user_prefix[] = "\\Registry\\User\\S-1-5-21-0-0-0-1000\\";

NTSTATUS WINAPI NtQueryInformationToken(HANDLE token, TOKEN_INFORMATION_CLASS class, void *info, ULONG length,
                                        ULONG *retlen) {
    (void)token;
    if (class != TokenUser) return STATUS_NOT_IMPLEMENTED;
    ULONG need = sizeof(TOKEN_USER) + offsetof(SID, SubAuthority[5]);
    if (retlen) *retlen = need;
    if (length < need) return STATUS_BUFFER_TOO_SMALL;
    TOKEN_USER *tu = info;
    SID *sid = (SID *)(tu + 1);
    static const SID_IDENTIFIER_AUTHORITY nt = { { 0, 0, 0, 0, 0, 5 } };
    sid->Revision = 1; sid->SubAuthorityCount = 5; sid->IdentifierAuthority = nt;
    sid->SubAuthority[0] = 21; sid->SubAuthority[1] = 0; sid->SubAuthority[2] = 0; sid->SubAuthority[3] = 0;
    sid->SubAuthority[4] = 1000;
    tu->User.Sid = sid; tu->User.Attributes = 0;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtCreateKey(HANDLE *retkey, ACCESS_MASK access, const OBJECT_ATTRIBUTES *attr, ULONG index,
                            const UNICODE_STRING *class, ULONG options, ULONG *dispos) {
    (void)access; (void)index; (void)class; (void)options;
    char path[2048];
    int n = ntdll_wcstoumbs(attr->ObjectName->Buffer, attr->ObjectName->Length / sizeof(WCHAR), path, sizeof path - 1, FALSE);
    if (n < 0) return STATUS_OBJECT_NAME_INVALID;
    path[n] = 0;
    pthread_mutex_lock(&keys_lock);
    int slot = -1;
    for (int i = 0; i < MAX_KEYS; i++) if (!keys[i]) { slot = i; break; }
    if (slot >= 0) keys[slot] = strdup(path);
    pthread_mutex_unlock(&keys_lock);
    if (slot < 0 || !keys[slot]) return STATUS_INSUFFICIENT_RESOURCES;
    *retkey = (HANDLE)(UINT_PTR)(slot + 1);
    if (dispos) *dispos = REG_OPENED_EXISTING_KEY;
    return STATUS_SUCCESS;
}

NTSTATUS WINAPI NtSetValueKey(HANDLE key, const UNICODE_STRING *name, ULONG index, ULONG type, const void *data,
                              ULONG count) {
    (void)index;
    UINT_PTR slot = (UINT_PTR)key - 1;
    char kpath[2048], vname[512];
    pthread_mutex_lock(&keys_lock);
    const char *k = slot < MAX_KEYS ? keys[slot] : NULL;
    if (k) snprintf(kpath, sizeof kpath, "%s", k);
    pthread_mutex_unlock(&keys_lock);
    if (!k) return STATUS_INVALID_HANDLE;
    int n = ntdll_wcstoumbs(name->Buffer, name->Length / sizeof(WCHAR), vname, sizeof vname - 1, FALSE);
    if (n < 0) return STATUS_OBJECT_NAME_INVALID;
    vname[n] = 0;
    const char *rel = !strncasecmp(kpath, user_prefix, sizeof user_prefix - 1) ? kpath + sizeof user_prefix - 1 : kpath;
    if (!thread_log) return STATUS_SUCCESS; /* not inside a forwarded call: nothing to replay to */
    return mn_reg_log_add(thread_log, type, rel, strlen(rel), vname, (size_t)n, data, count) ? STATUS_NO_MEMORY
                                                                                              : STATUS_SUCCESS;
}

NTSTATUS WINAPI NtClose(HANDLE key) {
    UINT_PTR slot = (UINT_PTR)key - 1;
    if (slot >= MAX_KEYS) return STATUS_INVALID_HANDLE;
    pthread_mutex_lock(&keys_lock);
    free(keys[slot]); keys[slot] = NULL;
    pthread_mutex_unlock(&keys_lock);
    return STATUS_SUCCESS;
}

/* ---- debug output ---- */

static int debug_on(void) { static int on = -1; if (on < 0) { const char *e = getenv("MN_STEAM_DEBUG"); on = e && *e == '1'; } return on; }

int __cdecl __wine_dbg_header(enum __wine_debug_class cls, struct __wine_debug_channel *channel, const char *function) {
    static const char *const classes[] = { "fixme", "err", "warn", "trace" };
    if (!debug_on() && cls != __WINE_DBCL_ERR) return -1;
    return fprintf(stderr, "%s:%s:%s ", (unsigned)cls < 4 ? classes[cls] : "?", channel->name, function);
}
int __cdecl __wine_dbg_output(const char *str) { return fputs(str, stderr); }
const char * __cdecl __wine_dbg_strdup(const char *str) {
    static __thread char ring[16][1024]; static __thread unsigned next;
    char *b = ring[next++ % 16];
    snprintf(b, sizeof ring[0], "%s", str);
    return b;
}
