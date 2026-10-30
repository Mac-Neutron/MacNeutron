/* wm_probe's driver: loading, the probe-held table, the runner (worker thread, children), the line printer and the
 * helpers of probe.h. Usage:
 *   wm_probe <host-dir> fixed|full [g0 ... g10] [--counts]   <host-dir>/ntdll.so, then winemetal.so (loader.c)
 *   wm_probe native <dylib> fixed|full [g0 ... g10] [--counts]
 *   wm_probe <host-dir> table        "table <slot> <offset|null>" for 0-157, "wow64 <slot> ..." when exported
 *   wm_probe <host-dir> rate         "rate <slot> <ns per call>" for 36, 37, 38
 *   wm_probe <host-dir> pair <native-dylib>   both images in one process, slot 117 through each table
 *   wm_probe --list                  "<group> <slot> <case> <flags>" for every case, without loading
 * Children (isolate and env cases) are the same command with --case <group> <name>. Fixtures: WM_PROBE_DIR (default
 * the source folder the probe was built from). */
#include "probe.h"
#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#include "loader.h"
#include <CommonCrypto/CommonDigest.h>
#include <copyfile.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <malloc/malloc.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <removefile.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *);

#define NSLOTS 158

id<MTLDevice> wm_device;
id<MTLCommandQueue> wm_queue;
const char *const wm_env_aniso[] = {"8", "0", "17", "8x", NULL};

static const struct wm_case *const tables[] = {wm_cases_g0, wm_cases_g1, wm_cases_g2, wm_cases_g3,
                                               wm_cases_g4, wm_cases_g5, wm_cases_g6, wm_cases_g7,
                                               wm_cases_g8, wm_cases_g9, wm_cases_g10};

static char self_path[PATH_MAX];
static const char *probe_dir;
static bool full_mode, native_mode, counts_flag;
static const char *image_arg; /* host dir, or the native dylib */
static void *image;
static wm_unix_fn table[NSLOTS];
static char slot_group[NSLOTS][4];
static pthread_t worker;

/* The case running, its slot, its counts. */
static const struct wm_case *cur;
static int cur_slot = -1;
static unsigned counts[NSLOTS];
static bool muted;

/* ---- the line printer ---- */

static pthread_mutex_t lock;
static char *pending;
static size_t pending_len, pending_cap;

/* One line: a newline inside a value prints as \n. */
static void out_line(const char *s) {
    if (muted) return;
    for (; *s; s++)
        if (*s == '\n') fputs("\\n", stdout);
        else fputc(*s, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

static void flush_pending(void) {
    pthread_mutex_lock(&lock);
    if (pending_len) out_line(pending);
    pending_len = 0;
    pthread_mutex_unlock(&lock);
}

static void vappend(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (pending_len + (size_t)n + 2 > pending_cap) {
        pending_cap = (pending_len + (size_t)n + 2) * 2;
        pending = realloc(pending, pending_cap);
        if (!pending) abort();
    }
    vsnprintf(pending + pending_len, (size_t)n + 1, fmt, ap);
    pending_len += (size_t)n;
}

static void append(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
}

static void start_line(void) {
    if (cur_slot >= 0) append("%s %d %s", cur ? cur->group : "-", cur_slot, cur ? cur->name : "-");
    else append("%s - %s", cur ? cur->group : "-", cur ? cur->name : "-");
}

void wm_line(const char *fmt, ...) {
    pthread_mutex_lock(&lock);
    flush_pending();
    start_line();
    append(" ");
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
    flush_pending();
    pthread_mutex_unlock(&lock);
}

/* A field of the current line (a line of its own, "<prefix> k=v", when no call started one). */
static void field(const char *fmt, ...) {
    pthread_mutex_lock(&lock);
    if (!pending_len) start_line();
    append(" ");
    va_list ap;
    va_start(ap, fmt);
    vappend(fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&lock);
}

static void stop(void) {
    @throw [NSException exceptionWithName:@"WMProbeStop" reason:nil userInfo:nil];
}

const char *wm_thread(void) {
    if (pthread_main_np()) return "main";
    return pthread_equal(pthread_self(), worker) ? "worker" : "other";
}

/* ---- handles ---- */

static CFMutableDictionaryRef hnum; /* handle -> number, per case */
static unsigned hnext;

const char *wm_hs(uint64_t h) {
    static __thread char buf[8][24];
    static __thread unsigned i;
    if (!h) return "0";
    char *b = buf[i++ % 8];
    pthread_mutex_lock(&lock);
    uintptr_t n = (uintptr_t)CFDictionaryGetValue(hnum, (const void *)(uintptr_t)h);
    if (!n && !muted) {
        n = ++hnext;
        CFDictionarySetValue(hnum, (const void *)(uintptr_t)h, (const void *)n);
    }
    pthread_mutex_unlock(&lock);
    if (n) snprintf(b, sizeof buf[0], "h%lu", (unsigned long)n);
    else snprintf(b, sizeof buf[0], "h?");
    return b;
}

void wm_h(const char *name, uint64_t h) {
    if (!h || h == WM_FILL64) {
        field("%s=%s", name, h ? "fill" : "0");
        flush_pending();
        wm_line("bad-handle");
        stop();
    }
    field("%s=%s", name, wm_hs(h));
}

void wm_h0(const char *name, uint64_t h) {
    if (h == WM_FILL64) wm_h(name, h);
    field("%s=%s", name, wm_hs(h));
}

/* ---- ownership ---- */

static struct {
    const char *name;
    const obj_handle_t *field;
} owns[8];
static int nowns;
static CFMutableDictionaryRef kept; /* object (one probe retain) -> its count after its last call */
static struct {
    id obj;
    NSUInteger count;
} ins[64];
static int nins;

static bool tagged(uint64_t h) { return (int64_t)h < 0; }

void wm_own(const char *name, const obj_handle_t *f) {
    if (muted) return;
    if (nowns == 8) {
        wm_line("too-many-owns");
        stop();
    }
    owns[nowns].name = name;
    owns[nowns++].field = f;
}

int wm_in(id obj) {
    if (muted || !obj) return 0;
    if (nins == 64) {
        wm_line("too-many-inputs");
        stop();
    }
    ins[nins].obj = [obj retain];
    ins[nins].count = [obj retainCount];
    return ++nins;
}

/* ---- calls ---- */

static unsigned stub(void *params) { return 0; }

unsigned wm_call(unsigned slot, void *params) {
    flush_pending();
    if (slot >= NSLOTS) {
        wm_line("bad-slot %u", slot);
        stop();
    }
    cur_slot = (int)slot;
    counts[slot]++;
    if (cur && strcmp(slot_group[slot], cur->group)) wm_line("cross %u", slot);
    wm_unix_fn fn = table[slot];
    if (!fn) {
        field("null");
        nowns = 0;
        return 0;
    }
    for (int k = 0; k < nins; k++) ins[k].count = [ins[k].obj retainCount];
    void *pool = objc_autoreleasePoolPush();
    unsigned st = 0;
    const char *raised = NULL;
    @try {
        st = fn(params);
    } @catch (NSException *e) {
        raised = strdup(e.name.UTF8String);
    } @catch (id e) {
        raised = strdup("non-NSException");
    }
    uint64_t v[8];
    NSUInteger before[8];
    bool first[8];
    for (int i = 0; i < nowns; i++) {
        v[i] = *owns[i].field;
        if (!v[i] || v[i] == WM_FILL64 || tagged(v[i])) continue;
        id o = (id)v[i];
        first[i] = !CFDictionaryContainsKey(kept, o);
        if (first[i]) {
            [o retain];
            CFDictionarySetValue(kept, o, (const void *)0);
        }
        before[i] = [o retainCount];
    }
    objc_autoreleasePoolPop(pool);
    if (raised) field("raised %s", raised);
    else field("%x", st);
    free((void *)raised);
    for (int i = 0; i < nowns; i++) {
        const char *n = owns[i].name;
        if (!v[i] || v[i] == WM_FILL64) {
            field("%s=%s", n, v[i] ? "fill" : "0");
            continue;
        }
        if (tagged(v[i])) {
            field("%s=%s own=n/a pool=n/a", n, wm_hs(v[i]));
            continue;
        }
        id o = (id)v[i];
        NSUInteger after = [o retainCount];
        NSUInteger last = (NSUInteger)CFDictionaryGetValue(kept, o);
        CFDictionarySetValue(kept, o, (const void *)after);
        if (before[i] >= 1u << 31 || after >= 1u << 31) field("%s=%s own=n/a pool=n/a", n, wm_hs(v[i]));
        else
            field("%s=%s own=%+ld pool=%+ld", n, wm_hs(v[i]), first[i] ? (long)after - 1 : (long)after - (long)last,
                  (long)before[i] - (long)after);
    }
    nowns = 0;
    for (int k = 0; k < nins; k++) {
        NSUInteger now = [ins[k].obj retainCount];
        if (now != ins[k].count) field("in%d=%+ld", k + 1, (long)now - (long)ins[k].count);
    }
    return st;
}

/* ---- fields ---- */

/* A value with a space or a quote prints quoted (\" and \\ escaped), so a field stays one token. */
static void vfield(const char *name, const char *fmt, va_list ap) {
    char *s;
    vasprintf(&s, fmt, ap);
    if (!strpbrk(s, " \"")) {
        field("%s=%s", name, s);
    } else {
        NSMutableString *q = [NSMutableString stringWithUTF8String:s];
        [q replaceOccurrencesOfString:@"\\" withString:@"\\\\" options:0 range:NSMakeRange(0, q.length)];
        [q replaceOccurrencesOfString:@"\"" withString:@"\\\"" options:0 range:NSMakeRange(0, q.length)];
        field("%s=\"%s\"", name, q.UTF8String);
    }
    free(s);
}

void wm_val(const char *name, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfield(name, fmt, ap);
    va_end(ap);
}

void wm_raw(const char *name, const char *fmt, ...) {
    if (!full_mode) return;
    va_list ap;
    va_start(ap, fmt);
    vfield(name, fmt, ap);
    va_end(ap);
}

void wm_rel(const char *name, bool holds, const char *relation, const char *fmt, ...) {
    if (holds && !full_mode) {
        field("%s=%s", name, relation);
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vfield(name, fmt, ap);
    va_end(ap);
}

void wm_eq(const char *name, uint64_t value, uint64_t oracle, const char *oracle_name) {
    wm_rel(name, value == oracle, oracle_name, "0x%llx", (unsigned long long)value);
}

void wm_bytes(const char *name, const void *p, size_t size) {
    const uint8_t *b = p;
    char *s = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&s, &len);
    for (size_t i = 0; i < size;) {
        if (b[i] == WM_FILL) {
            i++;
            continue;
        }
        size_t j = i;
        while (j + 1 < size && b[j + 1] != WM_FILL) j++;
        fprintf(f, "%s%zu-%zu", ftell(f) ? "," : "", i, j);
        i = j + 1;
    }
    fclose(f);
    field("%s=%s", name, len ? s : "none");
    free(s);
}

void wm_sha(const char *name, const void *p, size_t size) {
    uint8_t sum[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(p, (CC_LONG)size, sum);
    char hex[2 * CC_SHA256_DIGEST_LENGTH + 1];
    for (unsigned i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", sum[i]);
    field("%s=sha256:%s/%zu", name, hex, size);
}

void wm_send_refs(mach_port_t port) {
    mach_port_urefs_t refs = 0;
    kern_return_t kr = mach_port_get_refs(mach_task_self(), port, MACH_PORT_RIGHT_SEND, &refs);
    if (kr) field("send-refs=kr%d", kr);
    else field("send-refs=%u", refs);
}

void wm_fill(void *p, size_t size) { memset(p, WM_FILL, size); }

uint64_t wm_word(uint64_t handle) { return handle ? *(const uint64_t *)(uintptr_t)handle : 0; }

/* ---- the run ---- */

bool wm_full(void) { return full_mode; }
bool wm_native(void) { return native_mode; }
const char *wm_env(void) { return getenv("WM_PROBE_ENV"); }
void *wm_sym(const char *name) { return image ? dlsym(image, name) : NULL; }
void wm_event_hook(void (*hook)(uint64_t nt_event)) { mn_wm_set_event_hook(hook); }

const char *wm_child_stderr(void) {
    const char *path = getenv("WM_PROBE_STDERR");
    if (!path) return NULL;
    fflush(stderr);
    NSString *s = [NSString stringWithContentsOfFile:@(path) encoding:NSUTF8StringEncoding error:NULL];
    return s ? s.UTF8String : "";
}

static char tmpdir[PATH_MAX];
static bool tmpdir_mine;

static void remove_tmpdir(void) {
    if (tmpdir_mine) removefile(tmpdir, NULL, REMOVEFILE_RECURSIVE);
}

const char *wm_tmpdir(void) {
    if (tmpdir[0]) return tmpdir;
    const char *inherited = getenv("WM_TMPDIR");
    if (inherited) {
        snprintf(tmpdir, sizeof tmpdir, "%s", inherited);
        return tmpdir;
    }
    const char *t = getenv("TMPDIR");
    snprintf(tmpdir, sizeof tmpdir, "%s/wm-probe.XXXXXX", t && *t ? t : "/tmp");
    if (!mkdtemp(tmpdir)) {
        perror("wm_probe: mkdtemp");
        exit(2);
    }
    tmpdir_mine = true;
    atexit(remove_tmpdir);
    return tmpdir;
}

const char *wm_fixture_path(const char *name) {
    return [NSString stringWithFormat:@"%s/%s", probe_dir, name].fileSystemRepresentation;
}

NSData *wm_fixture(const char *name) { return [NSData dataWithContentsOfFile:@(wm_fixture_path(name))]; }

/* ---- children ---- */

static void put_env(char ***env, size_t *n, const char *kv) {
    *env = realloc(*env, (*n + 2) * sizeof **env);
    (*env)[(*n)++] = strdup(kv);
    (*env)[*n] = NULL;
}

/* The case c in a child, the probe on `image_path` (NULL: this one's) with the extra environment; its lines go to
 * our stdout; "hang" past 10 s, "crashed (signal n)", "exit n". */
static void spawn_case(const struct wm_case *c, const char *path, const char *const *extra, const char *variant) {
    flush_pending();
    fflush(stdout);
    const char *argv[16];
    int a = 0;
    argv[a++] = self_path;
    if (native_mode) argv[a++] = "native";
    argv[a++] = path ? path : image_arg;
    argv[a++] = full_mode ? "full" : "fixed";
    argv[a++] = "--case";
    argv[a++] = c->group;
    argv[a++] = c->name;
    if (counts_flag) argv[a++] = "--counts";
    argv[a] = NULL;
    char **env = NULL;
    size_t n = 0;
    for (char **e = environ; *e; e++) put_env(&env, &n, *e);
    for (; extra && *extra; extra++) put_env(&env, &n, *extra);
    char kv[PATH_MAX + 32];
    snprintf(kv, sizeof kv, "WM_TMPDIR=%s", wm_tmpdir());
    put_env(&env, &n, kv);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (variant) {
        snprintf(kv, sizeof kv, "WM_PROBE_ENV=%s", variant);
        put_env(&env, &n, kv);
        static unsigned k;
        char err[PATH_MAX];
        snprintf(err, sizeof err, "%s/stderr-%d-%u", wm_tmpdir(), getpid(), k++);
        snprintf(kv, sizeof kv, "WM_PROBE_STDERR=%s", err);
        put_env(&env, &n, kv);
        posix_spawn_file_actions_addopen(&fa, 2, err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    pid_t pid;
    int rc = posix_spawn(&pid, self_path, &fa, NULL, (char *const *)argv, env);
    posix_spawn_file_actions_destroy(&fa);
    for (size_t i = 0; i < n; i++) free(env[i]);
    free(env);
    const struct wm_case *was = cur;
    cur = c;
    cur_slot = (int)c->slot;
    if (rc) {
        wm_line("spawn-failed %s", strerror(rc));
    } else {
        int st = 0;
        uint64_t end = clock_gettime_nsec_np(CLOCK_MONOTONIC) + 10000000000ull;
        for (;;) {
            if (waitpid(pid, &st, WNOHANG) == pid) break;
            if (clock_gettime_nsec_np(CLOCK_MONOTONIC) > end) {
                kill(pid, SIGKILL);
                waitpid(pid, &st, 0);
                st = -1;
                break;
            }
            usleep(10000);
        }
        if (st == -1) wm_line("hang");
        else if (WIFSIGNALED(st)) wm_line("crashed (signal %d)", WTERMSIG(st));
        else if (WEXITSTATUS(st)) wm_line("exit %d", WEXITSTATUS(st));
    }
    cur = was;
    cur_slot = -1;
}

/* The presenter beside a copy of the image's folder: the env case's "present" variant. */
static const char *present_copy(void) {
    static char dir[PATH_MAX];
    if (dir[0]) return dir;
    const char *p = getenv("WM_PRESENTER");
    NSString *presenter = @(p ? p : WM_PRESENTER_DEFAULT);
    snprintf(dir, sizeof dir, "%s/present", wm_tmpdir());
    mkdir(dir, 0755);
    NSMutableArray *files = [NSMutableArray arrayWithObjects:presenter,
                                            [presenter.stringByDeletingPathExtension stringByAppendingPathExtension:@"metallib"],
                                            nil];
    if (native_mode) [files addObject:@(image_arg)];
    else
        for (NSString *f in @[ @"ntdll.so", @"winemac.so", @"winemetal.so" ])
            [files addObject:[@(image_arg) stringByAppendingPathComponent:f]];
    for (NSString *f in files) {
        NSString *to = [@(dir) stringByAppendingPathComponent:f.lastPathComponent];
        if (copyfile(f.fileSystemRepresentation, to.fileSystemRepresentation, NULL, COPYFILE_ALL | COPYFILE_CLONE)) {
            dir[0] = 0;
            return NULL;
        }
    }
    if (native_mode) strlcat(dir, [@"/" stringByAppendingString:@(image_arg).lastPathComponent].UTF8String, sizeof dir);
    return dir;
}

/* ---- cases ---- */

static NSMutableArray *dump_keys_extra;
NSMutableArray *wm__dump_keys(void) { return dump_keys_extra; }

static void run_case(const struct wm_case *c) {
    cur = c;
    cur_slot = -1;
    memset(counts, 0, sizeof counts);
    CFDictionaryRemoveAllValues(hnum);
    hnext = 0;
    [dump_keys_extra removeAllObjects];
    void *pool = objc_autoreleasePoolPush();
    @try {
        c->run();
    } @catch (NSException *e) {
        if (![e.name isEqualToString:@"WMProbeStop"]) {
            flush_pending();
            wm_line("uncaught %s", e.name.UTF8String);
        }
    }
    flush_pending();
    muted = false;
    nowns = 0;
    wm_swizzles(false);
    objc_autoreleasePoolPop(pool);
    CFIndex n = CFDictionaryGetCount(kept);
    const void **keys = malloc((size_t)(n ? n : 1) * sizeof *keys);
    CFDictionaryGetKeysAndValues(kept, keys, NULL);
    CFDictionaryRemoveAllValues(kept);
    for (CFIndex i = 0; i < n; i++) [(id)keys[i] release];
    free(keys);
    for (int k = 0; k < nins; k++) [ins[k].obj release];
    nins = 0;
    if (counts_flag)
        for (unsigned s = 0; s < NSLOTS; s++)
            if (counts[s]) {
                cur_slot = (int)s;
                wm_line("calls %u", counts[s]);
            }
    cur = NULL;
    cur_slot = -1;
}

static const char *case_flags(unsigned f) {
    static char b[128];
    static const char *names[] = {"isolate", "env-aniso", "env-present", "full-only",
                                  "native",  "wine-only", "gmalloc",     "not-on-c"};
    b[0] = 0;
    for (unsigned i = 0; i < 8; i++)
        if (f & (1u << i)) {
            if (b[0]) strlcat(b, ",", sizeof b);
            strlcat(b, names[i], sizeof b);
        }
    return b[0] ? b : "-";
}

static bool selected[11];
static const char *only_group, *only_case;

static void run_cases(void) {
    for (unsigned g = 0; g < 11; g++) {
        if (!selected[g]) continue;
        for (const struct wm_case *c = tables[g]; c->name; c++) {
            if (only_case) {
                if (!strcmp(c->group, only_group) && !strcmp(c->name, only_case)) run_case(c);
                continue;
            }
            if (native_mode ? (c->flags & WM_WINE_ONLY) : (c->flags & WM_NATIVE)) continue;
            if (!full_mode && (c->flags & WM_FULL_ONLY)) continue;
            if (c->flags & WM_ENV_ANISO) {
                for (const char *const *v = wm_env_aniso; *v; v++) {
                    char kv[64];
                    snprintf(kv, sizeof kv, "DXMT_MAX_ANISOTROPY=%s", *v);
                    const char *extra[] = {kv, NULL};
                    spawn_case(c, NULL, extra, *v);
                }
            } else if (c->flags & WM_ENV_PRESENT) {
                const char *extra[] = {"MACNEUTRON_PRESENT=1", NULL};
                spawn_case(c, NULL, extra, "absent");
                const char *copy = present_copy();
                if (copy) spawn_case(c, copy, extra, "present");
                else {
                    cur = c;
                    cur_slot = (int)c->slot;
                    wm_line("no-presenter (WM_PRESENTER)");
                    cur = NULL;
                    cur_slot = -1;
                }
            } else if (c->flags & (WM_ISOLATE | WM_GMALLOC)) {
                const char *extra[] = {"DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib", NULL};
                spawn_case(c, NULL, (c->flags & WM_GMALLOC) ? extra : NULL, NULL);
            } else {
                run_case(c);
            }
        }
    }
}

/* ---- Metal helpers ---- */

void wm_on_main(void (^block)(void)) {
    if (pthread_main_np()) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

bool wm_main_blocked(void (^block)(void)) {
    flush_pending();
    dispatch_semaphore_t ready = dispatch_semaphore_create(0), done = dispatch_semaphore_create(0);
    __block bool hung = false;
    dispatch_async(dispatch_get_main_queue(), ^{
      dispatch_semaphore_signal(ready);
      if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC))) {
          hung = true;
          wm_line("hang");
      }
    });
    dispatch_semaphore_wait(ready, DISPATCH_TIME_FOREVER);
    block();
    dispatch_semaphore_signal(done);
    return hung;
}

void wm_heap_growth(void (^block)(void), unsigned cycles) {
    block();
    flush_pending();
    if (!cycles) cycles = 1;
    malloc_statistics_t a, b;
    muted = true;
    malloc_zone_statistics(NULL, &a);
    for (unsigned i = 0; i < cycles; i++) {
        void *pool = objc_autoreleasePoolPush();
        block();
        flush_pending();
        objc_autoreleasePoolPop(pool);
    }
    malloc_zone_statistics(NULL, &b);
    muted = false;
    long per = ((long)b.size_in_use - (long)a.size_in_use) / (long)cycles;
    if (full_mode) wm_line("heap-growth=%ld", per);
    else wm_line("heap-growth=%s", per > 64 ? "leaked" : "freed");
}

id<MTLLibrary> wm_library(NSString *msl) {
    NSError *e = nil;
    id<MTLLibrary> l = [wm_device newLibraryWithSource:msl options:nil error:&e];
    if (!l) wm_line("library-error %s", e.localizedDescription.UTF8String);
    return [l autorelease];
}

static unsigned bytes_per_pixel(MTLPixelFormat f) {
    switch (f) {
    case MTLPixelFormatA8Unorm: case MTLPixelFormatR8Unorm: case MTLPixelFormatR8Unorm_sRGB: case MTLPixelFormatR8Snorm:
    case MTLPixelFormatR8Uint: case MTLPixelFormatR8Sint: case MTLPixelFormatStencil8:
        return 1;
    case MTLPixelFormatR16Unorm: case MTLPixelFormatR16Snorm: case MTLPixelFormatR16Uint: case MTLPixelFormatR16Sint:
    case MTLPixelFormatR16Float: case MTLPixelFormatRG8Unorm: case MTLPixelFormatRG8Unorm_sRGB:
    case MTLPixelFormatRG8Snorm: case MTLPixelFormatRG8Uint: case MTLPixelFormatRG8Sint:
    case MTLPixelFormatDepth16Unorm:
        return 2;
    case MTLPixelFormatR32Uint: case MTLPixelFormatR32Sint: case MTLPixelFormatR32Float: case MTLPixelFormatRG16Unorm:
    case MTLPixelFormatRG16Snorm: case MTLPixelFormatRG16Uint: case MTLPixelFormatRG16Sint:
    case MTLPixelFormatRG16Float: case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatRGBA8Snorm: case MTLPixelFormatRGBA8Uint: case MTLPixelFormatRGBA8Sint:
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB: case MTLPixelFormatRGB10A2Unorm:
    case MTLPixelFormatRGB10A2Uint: case MTLPixelFormatRG11B10Float: case MTLPixelFormatRGB9E5Float:
    case MTLPixelFormatBGR10A2Unorm: case MTLPixelFormatDepth32Float:
        return 4;
    case MTLPixelFormatRG32Uint: case MTLPixelFormatRG32Sint: case MTLPixelFormatRG32Float:
    case MTLPixelFormatRGBA16Unorm: case MTLPixelFormatRGBA16Snorm: case MTLPixelFormatRGBA16Uint:
    case MTLPixelFormatRGBA16Sint: case MTLPixelFormatRGBA16Float:
        return 8;
    case MTLPixelFormatRGBA32Uint: case MTLPixelFormatRGBA32Sint: case MTLPixelFormatRGBA32Float:
        return 16;
    default:
        return 0;
    }
}

NSData *wm_readback(id resource) {
    id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    id<MTLBuffer> out;
    if ([resource conformsToProtocol:@protocol(MTLBuffer)]) {
        id<MTLBuffer> b = resource;
        out = [wm_device newBufferWithLength:b.length options:MTLResourceStorageModeShared];
        [blit copyFromBuffer:b sourceOffset:0 toBuffer:out destinationOffset:0 size:b.length];
    } else {
        id<MTLTexture> t = resource;
        unsigned bpp = bytes_per_pixel(t.pixelFormat);
        if (!bpp) {
            [blit endEncoding];
            return nil;
        }
        NSUInteger row = t.width * bpp, image = row * t.height;
        out = [wm_device newBufferWithLength:image options:MTLResourceStorageModeShared];
        [blit copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(t.width, t.height, 1) toBuffer:out destinationOffset:0
          destinationBytesPerRow:row destinationBytesPerImage:image];
    }
    [blit endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    NSData *d = [NSData dataWithBytes:out.contents length:out.length];
    [out release];
    return d;
}

NSArray *wm_nsarray(unsigned n) {
    NSMutableArray *a = [NSMutableArray arrayWithCapacity:n];
    for (unsigned i = 0; i < n; i++) [a addObject:[[[NSObject alloc] init] autorelease]];
    return [NSArray arrayWithArray:a];
}

@interface WMProbeMetalView : NSView
@end
@implementation WMProbeMetalView
- (CALayer *)makeBackingLayer {
    CAMetalLayer *l = [CAMetalLayer layer];
    l.device = wm_device;
    return l;
}
@end

struct wm_window wm_window(void) {
    static struct wm_window w;
    if (w.window) return w;
    wm_on_main(^{
      [NSApplication sharedApplication];
      NSRect r = NSMakeRect(80, 80, 320, 180);
      w.window = [[NSWindow alloc] initWithContentRect:r
                                             styleMask:NSWindowStyleMaskTitled
                                               backing:NSBackingStoreBuffered
                                                 defer:NO
                                                screen:NSScreen.mainScreen];
      w.window.releasedWhenClosed = NO;
      w.window.title = @"wm_probe";
      w.view = [[WMProbeMetalView alloc] initWithFrame:NSMakeRect(0, 0, 320, 180)];
      w.view.wantsLayer = YES;
      w.window.contentView = w.view;
      w.layer = (CAMetalLayer *)w.view.layer;
      [w.window orderFrontRegardless];
    });
    return w;
}

/* ---- modes ---- */

static void load_slots(void) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/slots.txt", probe_dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "wm_probe: %s: %s\n", path, strerror(errno));
        exit(2);
    }
    unsigned s;
    char g[8], sym[256];
    while (fscanf(f, "%u %7s %255s", &s, g, sym) == 3)
        if (s < NSLOTS) snprintf(slot_group[s], sizeof slot_group[s], "%s", g);
    fclose(f);
}

static const wm_unix_fn *load_image(const char *arg, bool native) {
    const wm_unix_fn *funcs;
    if (native) {
        image = dlopen(arg, RTLD_NOW | RTLD_LOCAL);
        funcs = image ? dlsym(image, "__wine_unix_call_funcs") : NULL;
        if (!funcs) {
            fprintf(stderr, "wm_probe: %s: %s\n", arg, image ? "no __wine_unix_call_funcs" : dlerror());
            exit(2);
        }
        return funcs;
    }
    if (mn_wm_load(arg)) exit(2);
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/winemetal.so", arg);
    image = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
    return mn_wm_funcs;
}

static void table_mode(const wm_unix_fn *funcs) {
    Dl_info d;
    if (!dladdr((const void *)funcs, &d)) exit(2);
    const char *base = d.dli_fbase;
    for (unsigned i = 0; i < NSLOTS; i++)
        if (funcs[i]) printf("table %u %lx\n", i, (unsigned long)((const char *)funcs[i] - base));
        else printf("table %u null\n", i);
    /* The WoW64 table's length isn't in the image: its entries are those up to the next symbol dladdr finds (the
     * table's extent as the symbol table has it), each null or code of this image, at most 158. */
    const wm_unix_fn *wow = dlsym(image, "__wine_unix_call_wow64_funcs");
    for (unsigned i = 0; wow && i < NSLOTS; i++) {
        if (!dladdr((const void *)&wow[i], &d) || d.dli_saddr != (void *)wow) break;
        if (wow[i]) {
            Dl_info e;
            if (!dladdr((const void *)wow[i], &e) || e.dli_fbase != base) break;
            printf("wow64 %u %lx\n", i, (unsigned long)((const char *)wow[i] - base));
        } else {
            printf("wow64 %u null\n", i);
        }
    }
}

/* Slots 36-38 on a fixed 64-command list each, real encoders: the median of 5 runs of 10 000 calls (100 batches of
 * 100, an encoder each, never committed). */
static void rate_mode(void) {
    id<MTLLibrary> lib = wm_library(@"#include <metal_stdlib>\nusing namespace metal;\n"
                                    @"kernel void k(device uint *b [[buffer(0)]], uint i [[thread_position_in_grid]])"
                                    @" { b[i] = i; }\n"
                                    @"vertex float4 v(uint i [[vertex_id]]) { return float4(0, 0, 0, 1); }\n"
                                    @"fragment half4 f() { return half4(1); }\n");
    if (!lib) exit(1);
    NSError *e = nil;
    id<MTLFunction> k = [[lib newFunctionWithName:@"k"] autorelease], vf = [[lib newFunctionWithName:@"v"] autorelease],
                    ff = [[lib newFunctionWithName:@"f"] autorelease];
    id<MTLComputePipelineState> cpso = [wm_device newComputePipelineStateWithFunction:k error:&e];
    MTLRenderPipelineDescriptor *rd = [[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    rd.vertexFunction = vf;
    rd.fragmentFunction = ff;
    rd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    id<MTLRenderPipelineState> rpso = [wm_device newRenderPipelineStateWithDescriptor:rd error:&e];
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                  width:64
                                                                                 height:64
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> rt = [wm_device newTextureWithDescriptor:td];
    id<MTLBuffer> buf = [wm_device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
    if (!cpso || !rpso || !rt || !buf) exit(1);
    static uint8_t blit[64][64] __attribute__((aligned(16))), comp[64][64] __attribute__((aligned(16))),
        rend[64][64] __attribute__((aligned(16)));
    static const uint8_t bytes16[16];
    memset(blit, 0, sizeof blit), memset(comp, 0, sizeof comp), memset(rend, 0, sizeof rend);
    for (unsigned i = 0; i < 64; i++) {
        if (i % 2) {
            struct wmtcmd_blit_copy_from_buffer_to_buffer *c = (void *)blit[i];
            c->type = WMTBlitCommandCopyFromBufferToBuffer;
            c->src = (obj_handle_t)buf, c->src_offset = 0, c->dst = (obj_handle_t)buf, c->dst_offset = 2048;
            c->copy_length = 256;
        } else {
            struct wmtcmd_blit_fillbuffer *c = (void *)blit[i];
            c->type = WMTBlitCommandFillBuffer;
            c->buffer = (obj_handle_t)buf, c->offset = 0, c->length = 256, c->value = (uint8_t)i;
        }
        if (i == 0) {
            struct wmtcmd_compute_setpso *c = (void *)comp[i];
            c->type = WMTComputeCommandSetPSO;
            c->pso = (obj_handle_t)cpso;
            c->threadgroup_size = (struct WMTSize){1, 1, 1};
            struct wmtcmd_render_setpso *r = (void *)rend[i];
            r->type = WMTRenderCommandSetPSO;
            r->pso = (obj_handle_t)rpso;
        } else if (i % 3 == 1) {
            struct wmtcmd_compute_setbuffer *c = (void *)comp[i];
            c->type = WMTComputeCommandSetBuffer;
            c->buffer = (obj_handle_t)buf, c->offset = 0, c->index = 0;
            struct wmtcmd_render_setbuffer *r = (void *)rend[i];
            r->type = WMTRenderCommandSetVertexBuffer;
            r->buffer = (obj_handle_t)buf, r->offset = 0, r->index = 0;
        } else if (i % 3 == 2) {
            struct wmtcmd_compute_setbytes *c = (void *)comp[i];
            c->type = WMTComputeCommandSetBytes;
            c->bytes.ptr = (void *)bytes16, c->length = 16, c->index = 1;
            struct wmtcmd_render_setviewport *r = (void *)rend[i];
            r->type = WMTRenderCommandSetViewport;
            r->viewport = (struct WMTViewport){0, 0, 64, 64, 0, 1};
        } else {
            struct wmtcmd_compute_dispatch *c = (void *)comp[i];
            c->type = WMTComputeCommandDispatch;
            c->size = (struct WMTSize){1, 1, 1};
            struct wmtcmd_render_draw *r = (void *)rend[i];
            r->type = WMTRenderCommandDraw;
            r->primitive_type = WMTPrimitiveTypeTriangle, r->vertex_start = 0, r->vertex_count = 3;
            r->instance_count = 1, r->base_instance = 0;
        }
        if (i < 63) {
            ((struct wmtcmd_base *)blit[i])->next.ptr = blit[i + 1];
            ((struct wmtcmd_base *)comp[i])->next.ptr = comp[i + 1];
            ((struct wmtcmd_base *)rend[i])->next.ptr = rend[i + 1];
        }
    }
    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = rt;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    for (unsigned slot = 36; slot <= 38; slot++) {
        double runs[5];
        for (unsigned r = 0; r < 5; r++) {
            uint64_t total = 0;
            for (unsigned batch = 0; batch < 100; batch++) {
                void *pool = objc_autoreleasePoolPush();
                id<MTLCommandBuffer> cb = [wm_queue commandBuffer];
                id<MTLCommandEncoder> enc = slot == 36   ? (id)[cb blitCommandEncoder]
                                            : slot == 37 ? (id)[cb computeCommandEncoder]
                                                         : (id)[cb renderCommandEncoderWithDescriptor:rp];
                struct unixcall_generic_obj_cmd_noret p = {.encoder = (obj_handle_t)enc};
                p.cmd_head.ptr = slot == 36 ? blit[0] : slot == 37 ? comp[0] : rend[0];
                uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
                for (unsigned i = 0; i < 100; i++) table[slot](&p);
                total += clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0;
                [enc endEncoding];
                objc_autoreleasePoolPop(pool);
            }
            runs[r] = total / 10000.0;
        }
        for (unsigned i = 0; i < 5; i++)
            for (unsigned j = i + 1; j < 5; j++)
                if (runs[j] < runs[i]) {
                    double t = runs[i];
                    runs[i] = runs[j], runs[j] = t;
                }
        printf("rate %u %.1f\n", slot, runs[2]);
        fflush(stdout);
    }
}

static const char *pair_dylib;

static void pair_mode(void) {
    void *native = dlopen(pair_dylib, RTLD_NOW | RTLD_LOCAL);
    const wm_unix_fn *nfuncs = native ? dlsym(native, "__wine_unix_call_funcs") : NULL;
    if (!nfuncs) {
        fprintf(stderr, "wm_probe: %s: %s\n", pair_dylib, native ? "no __wine_unix_call_funcs" : dlerror());
        exit(2);
    }
    const struct {
        const char *label;
        wm_unix_fn fn;
    } images[] = {{"wine", table[117]}, {"native", nfuncs[117]}};
    for (unsigned i = 0; i < 2; i++) {
        char db[PATH_MAX];
        snprintf(db, sizeof db, "%s/pair-%s.db", wm_tmpdir(), images[i].label);
        struct unixcall_cache_alloc_init p;
        wm_fill(&p, sizeof p);
        p.path.ptr = db;
        p.version = 1;
        unsigned st = images[i].fn(&p);
        printf("pair %s 117 %x ret=%s class=%s\n", images[i].label, st,
               !p.ret_cache ? "0" : p.ret_cache == WM_FILL64 ? "fill" : "nonnull",
               p.ret_cache && p.ret_cache != WM_FILL64 ? object_getClassName((id)p.ret_cache) : "-");
    }
}

static enum { RUN, TABLE, RATE, PAIR } mode;

static void *worker_main(void *arg) {
    @autoreleasepool {
        if (mode == RATE) rate_mode();
        else if (mode == PAIR) pair_mode();
        else run_cases();
    }
    fflush(stdout);
    exit(0);
}

static void usage(void) {
    fprintf(stderr, "usage: wm_probe <host-dir> fixed|full [g0 ... g10] [--counts] [--case <group> <name>]\n"
                    "       wm_probe native <dylib> fixed|full [g0 ... g10] [--counts] [--case <group> <name>]\n"
                    "       wm_probe <host-dir> table|rate | <host-dir> pair <native-dylib> | --list\n");
    exit(2);
}

int main(int argc, char **argv) {
    uint32_t size = sizeof self_path;
    char exe[PATH_MAX];
    if (_NSGetExecutablePath(exe, &size) || !realpath(exe, self_path)) return 2;
    probe_dir = getenv("WM_PROBE_DIR") ? getenv("WM_PROBE_DIR") : WM_PROBE_SRC;
    const char *pos[4];
    int npos = 0;
    bool list = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--list")) list = true;
        else if (!strcmp(argv[i], "--counts")) counts_flag = true;
        else if (!strcmp(argv[i], "--case") && i + 2 < argc) only_group = argv[++i], only_case = argv[++i];
        else if (argv[i][0] == 'g' && argv[i][1] >= '0' && argv[i][1] <= '9' && npos >= 2) {
            int g = atoi(argv[i] + 1);
            if (g > 10) usage();
            selected[g] = true;
        } else if (npos < 4) pos[npos++] = argv[i];
        else usage();
    }
    if (list) {
        for (unsigned g = 0; g < 11; g++)
            for (const struct wm_case *c = tables[g]; c->name; c++)
                printf("%s %u %s %s\n", c->group, c->slot, c->name, case_flags(c->flags));
        return 0;
    }
    if (npos >= 1 && !strcmp(pos[0], "native")) {
        native_mode = true;
        for (int i = 1; i < npos; i++) pos[i - 1] = pos[i];
        npos--;
    }
    if (npos < 2) usage();
    image_arg = pos[0];
    if (!strcmp(pos[1], "fixed") || !strcmp(pos[1], "full")) {
        mode = RUN;
        full_mode = !strcmp(pos[1], "full");
        if (npos != 2) usage();
    } else if (!strcmp(pos[1], "table") && npos == 2) mode = TABLE;
    else if (!strcmp(pos[1], "rate") && npos == 2) mode = RATE;
    else if (!strcmp(pos[1], "pair") && npos == 3) mode = PAIR, pair_dylib = pos[2];
    else usage();
    if (only_group) {
        int g = only_group[0] == 'g' ? atoi(only_group + 1) : -1;
        if (g < 0 || g > 10) usage();
        selected[g] = true;
    }
    bool any = false;
    for (unsigned g = 0; g < 11; g++) any |= selected[g];
    if (!any) memset(selected, 1, sizeof selected);
    /* full: NSZombieEnabled from the start, so an over-release is caught. */
    if (full_mode && !(getenv("NSZombieEnabled") && !strcmp(getenv("NSZombieEnabled"), "YES"))) {
        setenv("NSZombieEnabled", "YES", 1);
        execv(self_path, argv);
        perror("wm_probe: execv");
        return 2;
    }
    pthread_mutexattr_t ma;
    pthread_mutexattr_init(&ma);
    pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&lock, &ma);
    hnum = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    kept = CFDictionaryCreateMutable(NULL, 0, NULL, NULL);
    dump_keys_extra = [[NSMutableArray alloc] init];
    signal(SIGPIPE, SIG_IGN);

    const wm_unix_fn *funcs = load_image(image_arg, native_mode);
    if (mode == TABLE) {
        table_mode(funcs);
        return 0;
    }
    load_slots();
    memcpy(table, funcs, sizeof table);
    const char *stubs = getenv("WM_STUB");
    for (const char *s = stubs; s && *s;) {
        char *end;
        unsigned long n = strtoul(s, &end, 10);
        if (end == s || n >= NSLOTS) {
            fprintf(stderr, "wm_probe: WM_STUB=%s\n", stubs);
            return 2;
        }
        table[n] = (wm_unix_fn)stub;
        s = *end == ',' ? end + 1 : end;
    }
    wm_device = MTLCreateSystemDefaultDevice();
    wm_queue = [wm_device newCommandQueue];
    if (!wm_device || !wm_queue) {
        fprintf(stderr, "wm_probe: no Metal device\n");
        return 2;
    }
    pthread_attr_t pa;
    pthread_attr_init(&pa);
    pthread_attr_setstacksize(&pa, 8 << 20);
    if (pthread_create(&worker, &pa, worker_main, NULL)) return 2;
    CFRunLoopRun();
    return 0;
}
