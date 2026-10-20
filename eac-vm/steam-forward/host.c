/* mn-steam-host: runs the guest's forwarded lsteamclient Unix calls against the Mac's Steam (spec §5.9).
 *   mn-steam-host --socket <path> --appid <id> --map <host dir>=<guest dir> [--client-dir <Mac Steam folder>]
 *                 [--parent <pid>]
 * It loads the copy of MacNeutron's macOS lsteamclient.so beside it (whose ntdll.so imports resolve to our stand-ins,
 * host_wine.c), which loads Mac Steam's steamclient.dylib. One thread per connection: each guest thread gets its own,
 * so replies never cross. A malformed message closes that connection. Exit 2 on bad arguments, 3 when the Mac Steam
 * folder has no steamclient.dylib, 4 when lsteamclient.so can't be loaded. */
#include <dlfcn.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "unixlib.h"
#include "client.h"
#include "host_wine.h"

typedef unsigned int (*entry_t)(void *);
static const entry_t *funcs, *wow64_funcs;
static char tmppath[TEMP_PATH_BUFFER_LENGTH]; /* steamclient_init's path buffer stays on the host (gen.py) */

/* WoW64 calls (a 32-bit game's): Valve's WoW64 functions read their params' 32-bit pointers as addresses in this
 * process, so the pointees are copied below 4 GiB, into a region mapped at start-up. Mapping there needs
 * com.apple.developer.cross-architecture-support, which the kernel honours for an executable in a bundle with its
 * provisioning profile (mn-steam-host.app). Its first MiB is WoW64 steamclient_init's path buffer; then one slot per
 * connection that has made a WoW64 call, as large as a message.
 * ponytail: 32 slots, so 32 guest threads in 32-bit Steam calls at once; more when a game needs them. */
#define LOW_BASE 0x20000000ul
#define LOW_SLOT ((size_t)MN_MAX_MESSAGE + (4u << 20))
#define LOW_SLOTS 32
static uint8_t *low_region;
static uint32_t low_busy;
static pthread_mutex_t low_lock = PTHREAD_MUTEX_INITIALIZER;
static const char *low_init(void) {
    void *p = mmap((void *)LOW_BASE, (1u << 20) + LOW_SLOTS * LOW_SLOT, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return strerror(errno);
    low_region = p;
    return NULL;
}
static uint8_t *low_slot(int s) { return low_region + (1u << 20) + (size_t)s * LOW_SLOT; }
static int low_take(void) {
    int s = -1;
    pthread_mutex_lock(&low_lock);
    for (int i = 0; low_region && wow64_funcs && i < LOW_SLOTS && s < 0; i++)
        if (!(low_busy & (1u << i))) { low_busy |= 1u << i; s = i; }
    pthread_mutex_unlock(&low_lock);
    return s;
}
static void low_give(int s) {
    madvise(low_slot(s), LOW_SLOT, MADV_FREE);
    pthread_mutex_lock(&low_lock);
    low_busy &= ~(1u << s);
    pthread_mutex_unlock(&low_lock);
}

static void *serve(void *arg) {
    int fd = (int)(intptr_t)arg;
    uint32_t theirs, mine = mn_tables_checksum();
    int slot = -1; /* this connection's low slot, from its first WoW64 call */
    if (mn_read_full(fd, &theirs, 4)) { close(fd); return NULL; } /* closed before the handshake: a probe */
    if (mn_write_full(fd, &mine, 4) || theirs != mine) {
        fprintf(stderr, "mn-steam-host: guest table %08x differs from ours %08x\n", theirs, mine);
        close(fd);
        return NULL;
    }
    for (;;) {
        struct mn_req_hdr h;
        if (mn_read_full(fd, &h, sizeof h)) break;
        if (h.magic != MN_MAGIC || h.len > MN_MAX_MESSAGE) { fprintf(stderr, "mn-steam-host: bad header\n"); break; }
        uint8_t *msg = malloc(sizeof h + h.len);
        if (!msg) break;
        memcpy(msg, &h, sizeof h);
        if (mn_read_full(fd, msg + sizeof h, h.len)) { free(msg); break; }
        int wow64 = (h.index & MN_WOW64) != 0;
        /* Without a slot the call can't run: closing makes the guest answer as with no Steam (client.c no_steam). */
        if (wow64 && slot < 0 && (slot = low_take()) < 0) {
            fprintf(stderr, "mn-steam-host: a 32-bit call can't run (%s), closing\n",
                    low_region && wow64_funcs ? "no free slot" : "32-bit calls unavailable");
            free(msg);
            break;
        }
        const struct mn_call_desc *table = wow64 ? mn_calls_wow64 : mn_calls;
        uint32_t index; void *params;
        struct mn_arena arena = { .low = wow64 ? low_slot(slot) : NULL, .low_cap = wow64 ? LOW_SLOT : 0 };
        int r = mn_unpack_request(table, mn_ncalls, msg, sizeof h + h.len, &index, &params, &arena);
        free(msg);
        if (r) { fprintf(stderr, "mn-steam-host: malformed request (%d), closing\n", r); mn_arena_free(&arena); break; }
        if (index == unix_steamclient_init && wow64)
            ((struct wow64_steamclient_init_params *)params)->g_tmppath.value = (uint32_t)(uintptr_t)low_region;
        else if (index == unix_steamclient_init)
            ((struct steamclient_init_params *)params)->g_tmppath = tmppath;
        struct mn_reg_log log = {0};
        mn_stub_set_reg_log(&log);
        uint32_t status = (wow64 ? wow64_funcs : funcs)[index](params);
        mn_stub_set_reg_log(NULL);
        struct mn_buf rep = {0};
        r = mn_pack_reply(&table[index], status, params, &log, &rep);
        mn_buf_free(&log.entries);
        mn_arena_free(&arena);
        if (r || mn_write_full(fd, rep.data, rep.len)) { mn_buf_free(&rep); break; }
        mn_buf_free(&rep);
    }
    if (slot >= 0) low_give(slot);
    close(fd);
    return NULL;
}

/* --parent: exit when the VM host is gone, however it ended. */
static void *watch_parent(void *arg) {
    pid_t parent = (pid_t)(intptr_t)arg;
    while (kill(parent, 0) == 0 || errno == EPERM) sleep(1);
    _exit(0);
}

int main(int argc, char **argv) {
    const char *sock = NULL, *appid = NULL, *map = NULL, *client_dir = NULL, *parent = NULL;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--socket")) sock = argv[i + 1];
        else if (!strcmp(argv[i], "--appid")) appid = argv[i + 1];
        else if (!strcmp(argv[i], "--map")) map = argv[i + 1];
        else if (!strcmp(argv[i], "--client-dir")) client_dir = argv[i + 1];
        else if (!strcmp(argv[i], "--parent")) parent = argv[i + 1];
        else { fprintf(stderr, "mn-steam-host: unknown option %s\n", argv[i]); return 2; }
    }
    const char *eq = map ? strchr(map, '=') : NULL;
    if (!sock || !appid || !eq || (argc - 1) % 2) {
        fprintf(stderr, "usage: mn-steam-host --socket <path> --appid <id> --map <host dir>=<guest dir> [--client-dir <dir>]\n");
        return 2;
    }
    char host_root[PATH_MAX];
    snprintf(host_root, sizeof host_root, "%.*s", (int)(eq - map), map);
    mn_stub_set_map(host_root, eq + 1);

    char dir[PATH_MAX], def[PATH_MAX];
    if (!client_dir) {
        snprintf(def, sizeof def, "%s/Library/Application Support/Steam/Steam.AppBundle/Steam/Contents/MacOS", getenv("HOME"));
        client_dir = def;
    }
    snprintf(dir, sizeof dir, "%s/steamclient.dylib", client_dir);
    struct stat st;
    if (stat(dir, &st)) { fprintf(stderr, "mn-steam-host: no steamclient.dylib in %s\n", client_dir); return 3; }
    setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", client_dir, 1);
    setenv("SteamAppId", appid, 1);
    setenv("SteamGameId", appid, 1);

    uint32_t exe_len = sizeof dir;
    if (_NSGetExecutablePath(dir, &exe_len)) return 4;
    char lib[PATH_MAX];
    snprintf(lib, sizeof lib, "%s/lsteamclient.so", dirname(dir));
    void *h = dlopen(lib, RTLD_NOW);
    if (!h || !(funcs = dlsym(h, "__wine_unix_call_funcs"))) {
        fprintf(stderr, "mn-steam-host: cannot load %s: %s\n", lib, dlerror());
        return 4;
    }
    wow64_funcs = dlsym(h, "__wine_unix_call_wow64_funcs");
    const char *why = wow64_funcs ? low_init() : "no WoW64 table in lsteamclient.so";
    fprintf(stderr, "mn-steam-host: 32-bit calls: %s%s%s\n", why ? "unavailable (" : "ready", why ? why : "", why ? ")" : "");

    signal(SIGPIPE, SIG_IGN);
    if (parent) { pthread_t w; pthread_create(&w, NULL, watch_parent, (void *)(intptr_t)atoi(parent)); pthread_detach(w); }
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (strlen(sock) >= sizeof a.sun_path) { fprintf(stderr, "mn-steam-host: socket path too long\n"); return 2; }
    strcpy(a.sun_path, sock);
    unlink(sock);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 64)) {
        fprintf(stderr, "mn-steam-host: %s: %s\n", sock, strerror(errno));
        return 2;
    }
    fprintf(stderr, "mn-steam-host: serving app %s on %s\n", appid, sock);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) { if (errno == EINTR) continue; break; }
        pthread_t t;
        if (pthread_create(&t, NULL, serve, (void *)(intptr_t)c)) close(c);
        else pthread_detach(t);
    }
    return 0;
}
