/* wm_probe's g8 cases (slots.txt's g8; probe.h), recorded from DXMT's C: the shader caches (115 reader, 116 get, 117
 * writer, 118 set) over SQLite files, and 119, the Metal shader cache folder. Every database, lock and folder is under
 * wm_tmpdir() (a random folder per run: never printed), or, for the relative paths, under a name unique to the process
 * in the user cache folder (removed after). The database format is a seam (D18): tables, lock file, journal mode and
 * the bytes of the closed file are pinned. A writer or reader the case wants closed is released directly (no wm_own,
 * which keeps a retain until the case ends); the cache objects are released directly, never through slot 1. */
#include "probe.h"
#include <fcntl.h>
#include <pthread.h>
#include <removefile.h>
#include <sqlite3.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

extern NSString *MTLGetShaderCachePath(void);

static NSString *tmp(const char *rel) { return [NSString stringWithFormat:@"%s/%s", wm_tmpdir(), rel]; }

static bool exists(NSString *path) { return !access(path.fileSystemRepresentation, F_OK); }

static const char *yesno(bool b) { return b ? "yes" : "no"; }

static NSString *user_cache_dir(void) {
    char buf[PATH_MAX];
    return confstr(_CS_DARWIN_USER_CACHE_DIR, buf, sizeof buf) ? @(buf) : nil;
}

static NSData *key_for(unsigned i) {
    char b[16];
    snprintf(b, sizeof b, "key-%04u", i);
    return [NSData dataWithBytes:b length:8];
}

static NSData *value_for(unsigned i) {
    size_t n = 24 + i * 8;
    uint8_t *b = malloc(n);
    for (size_t j = 0; j < n; j++) b[j] = (uint8_t)(i * 31 + j);
    return [NSData dataWithBytesNoCopy:b length:n freeWhenDone:YES];
}

/* +1 */
static dispatch_data_t dd_of(NSData *d) {
    return dispatch_data_create(d.bytes, d.length, NULL, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
}

static NSData *flatten(dispatch_data_t d) {
    const void *b = NULL;
    size_t n = 0;
    dispatch_data_t m = dispatch_data_create_map(d, &b, &n);
    NSData *r = [NSData dataWithBytes:b length:n];
    [(id)m release];
    return r;
}

/* 115 or 117 on a path: the object (0 on failure), its class printed; with own, its ownership too. */
static uint64_t open_cache(unsigned slot, NSString *path, uint64_t version, bool own) {
    struct unixcall_cache_alloc_init p;
    wm_fill(&p, sizeof p);
    p.path.ptr = path.fileSystemRepresentation;
    p.version = version;
    if (own) wm_own("ret", &p.ret_cache);
    wm_call(slot, &p);
    if (!own) wm_val("ret", "%s", !p.ret_cache ? "0" : p.ret_cache == WM_FILL64 ? "fill" : "nonnull");
    if (!p.ret_cache || p.ret_cache == WM_FILL64) return 0;
    if (own) wm_val("class", "%s", object_getClassName((id)p.ret_cache));
    return p.ret_cache;
}

static bool watch_values; /* put() lets wm_in watch the value's retain count: the handler must not consume it */

static unsigned put(uint64_t writer, NSData *key, NSData *value) {
    dispatch_data_t v = dd_of(value);
    struct unixcall_cache_set p;
    wm_fill(&p, sizeof p);
    p.cache = writer;
    p.key.ptr = key.bytes;
    p.key_length = key.length;
    p.value_data = (uint64_t)v;
    if (watch_values) wm_in((id)v);
    unsigned st = wm_call(118, &p);
    [(id)v release];
    return st;
}

/* 116: the value (nil on a miss), the data object +1 (released here) with its ownership when own. */
static NSData *get(uint64_t reader, NSData *key, bool own) {
    struct unixcall_cache_get p;
    wm_fill(&p, sizeof p);
    p.cache = reader;
    p.key.ptr = key.bytes;
    p.key_length = key.length;
    if (own) wm_own("ret_data", &p.ret_data);
    wm_call(116, &p);
    if (!own) wm_val("ret_data", "%s", !p.ret_data ? "0" : p.ret_data == WM_FILL64 ? "fill" : "nonnull");
    if (!p.ret_data || p.ret_data == WM_FILL64) return nil;
    NSData *out = flatten((dispatch_data_t)p.ret_data);
    [(id)p.ret_data release];
    return out;
}

static void hit(const char *name, NSData *got, NSData *want) {
    wm_val(name, "%s", !got ? "miss" : [got isEqualToData:want] ? "bytes" : "other");
}

/* The first column of each row of a query on a closed or open database, joined by commas. */
static NSString *sql_rows(NSString *db, const char *sql) {
    sqlite3 *h = NULL;
    if (sqlite3_open_v2(db.fileSystemRepresentation, &h, SQLITE_OPEN_READWRITE, NULL)) {
        sqlite3_close(h);
        return @"(open-failed)";
    }
    NSMutableArray *rows = [NSMutableArray array];
    sqlite3_stmt *st = NULL;
    NSString *result;
    if (sqlite3_prepare_v2(h, sql, -1, &st, NULL) == SQLITE_OK) {
        while (sqlite3_step(st) == SQLITE_ROW) {
            NSMutableArray *cols = [NSMutableArray array];
            for (int c = 0; c < sqlite3_column_count(st); c++) {
                const char *t = (const char *)sqlite3_column_text(st, c);
                [cols addObject:t ? @(t) : @"NULL"];
            }
            [rows addObject:[cols componentsJoinedByString:@"/"]];
        }
        sqlite3_finalize(st);
        result = rows.count ? [rows componentsJoinedByString:@","] : @"(none)";
    } else {
        result = @"(prepare-failed)";
    }
    sqlite3_close(h);
    return result;
}

static void sql_exec(NSString *db, const char *sql) {
    sqlite3 *h = NULL;
    if (!sqlite3_open_v2(db.fileSystemRepresentation, &h, SQLITE_OPEN_READWRITE, NULL)) sqlite3_exec(h, sql, NULL, NULL, NULL);
    sqlite3_close(h);
}

static void classes(void) {
    wm_val("registered", "reader=%s writer=%s native-reader=%s native-writer=%s", yesno(NSClassFromString(@"CacheReader")),
           yesno(NSClassFromString(@"CacheWriter")), yesno(NSClassFromString(@"DXMTNativeCacheReader")),
           yesno(NSClassFromString(@"DXMTNativeCacheWriter")));
}

/* ---- the writer and the reader ---- */

/* 117, 118 x 3, a release, 115, 116 on hits and a miss. The writer is still open (the probe keeps its retain) when the
 * reader reads: WAL shows it the writes. At the case's end both close and the database file is complete: this case's
 * folder is what dxmt/tests/wm_probe/cache-c.db was cut from (WM_TMPDIR=<folder>), cross-read's input. */
static void writer_reader(void) {
    classes();
    NSString *db = tmp("writer-reader/cache.db");
    uint64_t w = open_cache(117, db, 1, true);
    if (!w) return;
    wm_val("db", "%s", yesno(exists(db)));
    wm_val("lock-file", "%s", yesno(exists([db stringByAppendingString:@"-lock"])));
    watch_values = true;
    for (unsigned i = 1; i <= 3; i++) put(w, key_for(i), value_for(i));
    watch_values = false;
    wm_val("wal", "%s", yesno(exists([db stringByAppendingString:@"-wal"])));
    uint64_t r = open_cache(115, db, 1, true);
    if (!r) return;
    for (unsigned i = 1; i <= 3; i++) hit("hit", get(r, key_for(i), true), value_for(i));
    hit("miss", get(r, key_for(9), true), value_for(9));
    hit("short-key", get(r, [NSData dataWithBytes:"key-0001" length:7], true), value_for(1));
    hit("long-key", get(r, [NSData dataWithBytes:"key-00011" length:9], true), value_for(1));
    [(id)r release]; /* the caller's +1; the probe's retain goes at the case's end, which closes both connections */
    [(id)w release];
}

/* The C's database file, committed (cache-c.db: the folder of writer-reader), read by the reader: the same values. */
static void cross_read(void) {
    NSData *fixture = wm_fixture("cache-c.db");
    if (!fixture) {
        wm_val("fixture", "missing");
        return;
    }
    NSString *dir = tmp("cross-read");
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *db = [dir stringByAppendingPathComponent:@"cache.db"];
    [fixture writeToFile:db atomically:NO];
    /* The writer leaves an empty -wal beside a WAL database (this SQLite keeps it); a read-only connection needs it. */
    [[NSData data] writeToFile:[db stringByAppendingString:@"-wal"] atomically:NO];
    uint64_t r = open_cache(115, db, 1, true);
    if (!r) return;
    for (unsigned i = 1; i <= 3; i++) hit("hit", get(r, key_for(i), true), value_for(i));
    hit("miss", get(r, key_for(9), true), value_for(9));
    [(id)r release];
}

/* INSERT OR REPLACE: the second value wins, one row. */
static void replace(void) {
    NSString *db = tmp("replace/cache.db");
    uint64_t w = open_cache(117, db, 1, false);
    if (!w) return;
    put(w, key_for(1), value_for(1));
    put(w, key_for(1), value_for(2));
    put(w, key_for(2), value_for(3));
    uint64_t r = open_cache(115, db, 1, false);
    hit("k1", get(r, key_for(1), false), value_for(2));
    hit("k2", get(r, key_for(2), false), value_for(3));
    wm_val("rows", "%s", sql_rows(db, "SELECT count(*) FROM cache_1").UTF8String);
    [(id)r release];
    [(id)w release];
}

/* Two writers on one file, each its own connection: both writes land. And a reader of another version finds nothing. */
static void concurrent_versions(void) {
    NSString *db = tmp("versions/cache.db");
    uint64_t a = open_cache(117, db, 1, false), b = open_cache(117, db, 1, false);
    if (!a || !b) return;
    put(a, key_for(1), value_for(1));
    put(b, key_for(2), value_for(2));
    put(a, key_for(3), value_for(3));
    [(id)a release];
    [(id)b release];
    uint64_t r = open_cache(115, db, 1, false);
    for (unsigned i = 1; i <= 3; i++) hit("hit", get(r, key_for(i), false), value_for(i));
    [(id)r release];
    uint64_t other = open_cache(115, db, 2, false);
    if (other) [(id)other release];
    static const uint64_t big[] = {0, 4294967296ull, UINT64_MAX};
    for (unsigned i = 0; i < sizeof big / sizeof *big; i++) {
        uint64_t w = open_cache(117, db, big[i], false);
        if (!w) continue;
        put(w, key_for(1), value_for(i + 5));
        [(id)w release];
        uint64_t rd = open_cache(115, db, big[i], false);
        hit("version-hit", get(rd, key_for(1), false), value_for(i + 5));
        [(id)rd release];
    }
    wm_val("tables", "%s", sql_rows(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").UTF8String);
}

/* ---- migration ---- */

/* A writer of a new version drops the other cache_* tables (GLOB, case-sensitive; nothing else), then VACUUMs if it
 * dropped any; with none to drop it doesn't. */
static void version_migrate(void) {
    NSString *db = tmp("migrate/cache.db");
    uint64_t w = open_cache(117, db, 1, false);
    if (!w) return;
    for (unsigned i = 0; i < 40; i++) {
        NSMutableData *v = [NSMutableData dataWithLength:2048];
        memset(v.mutableBytes, (int)i, v.length);
        put(w, key_for(i), v);
    }
    [(id)w release];
    wm_val("tables-v1", "%s", sql_rows(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").UTF8String);
    sql_exec(db, "CREATE TABLE other(x); CREATE TABLE cachefoo(x); CREATE TABLE Cache_upper(x);"
                 "CREATE TABLE cache_9(key BLOB PRIMARY KEY, value BLOB NOT NULL); CREATE TABLE cache_3(x);");
    wm_val("tables-before", "%s", sql_rows(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").UTF8String);
    long pages_before = atol(sql_rows(db, "PRAGMA page_count").UTF8String);
    uint64_t w2 = open_cache(117, db, 2, false);
    if (!w2) return;
    [(id)w2 release];
    wm_val("tables-after", "%s", sql_rows(db, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").UTF8String);
    long pages_after = atol(sql_rows(db, "PRAGMA page_count").UTF8String);
    wm_val("vacuumed", "%s", yesno(pages_after < pages_before && !strcmp(sql_rows(db, "PRAGMA freelist_count").UTF8String, "0")));
    wm_val("old-version", "%s", yesno(open_cache(115, db, 1, false)));
    /* No table to drop: a freelist (made here by deleting rows) survives a writer of the same version. */
    uint64_t w3 = open_cache(117, db, 2, false);
    for (unsigned i = 0; i < 40; i++) {
        NSMutableData *v = [NSMutableData dataWithLength:2048];
        put(w3, key_for(i), v);
    }
    [(id)w3 release];
    sql_exec(db, "DELETE FROM cache_2");
    long freed = atol(sql_rows(db, "PRAGMA freelist_count").UTF8String);
    uint64_t w4 = open_cache(117, db, 2, false);
    if (w4) [(id)w4 release];
    wm_val("freelist-kept", "%s", yesno(freed > 0 && atol(sql_rows(db, "PRAGMA freelist_count").UTF8String) == freed));
}

/* The bytes of the closed database after fixed write sequences (D18): a fresh file, then a migrated one. Closing
 * checkpoints (the -wal is left empty). Full mode only: SQLite's version is in the file. */
static void db_bytes(void) {
    NSString *a = tmp("bytes-a/cache.db"), *b = tmp("bytes-b/cache.db");
    uint64_t w = open_cache(117, a, 1, false);
    if (!w) return;
    for (unsigned i = 1; i <= 5; i++) put(w, key_for(i), value_for(i));
    put(w, key_for(2), value_for(7));
    [(id)w release];
    NSData *fa = [NSData dataWithContentsOfFile:a];
    wm_sha("fresh", fa.bytes, fa.length);
    wm_val("wal-bytes", "%lu", (unsigned long)[NSData dataWithContentsOfFile:[a stringByAppendingString:@"-wal"]].length);
    w = open_cache(117, b, 1, false);
    for (unsigned i = 1; i <= 30; i++) put(w, key_for(i), value_for(i + 100));
    [(id)w release];
    w = open_cache(117, b, 2, false);
    for (unsigned i = 1; i <= 3; i++) put(w, key_for(i), value_for(i));
    [(id)w release];
    NSData *fb = [NSData dataWithContentsOfFile:b];
    wm_sha("migrated", fb.bytes, fb.length);
}

/* ---- paths ---- */

/* A writer and a reader on one path form: whether each opens, where the file ends up, and whether the value reads back. */
static void try_form(const char *label, NSString *path, NSString *db_at) {
    wm_line("form=%s", label);
    uint64_t w = open_cache(117, path, 1, false);
    if (w) {
        put(w, key_for(1), value_for(1));
        [(id)w release];
    }
    wm_val("db-at", "%s", yesno(exists(db_at)));
    uint64_t r = open_cache(115, path, 1, false);
    if (r) {
        hit("hit", get(r, key_for(1), false), value_for(1));
        [(id)r release];
    }
}

/* Absolute paths: used as they are (no normalising), the parent of the file created, not the file's folder itself. */
static void path_forms(void) {
    NSString *base = tmp("forms");
    try_form("plain", [base stringByAppendingString:@"/plain/cache.db"], [base stringByAppendingString:@"/plain/cache.db"]);
    try_form("deep", [base stringByAppendingString:@"/deep/a/b/c/cache.db"], [base stringByAppendingString:@"/deep/a/b/c/cache.db"]);
    try_form("doubled", [base stringByAppendingString:@"//dbl//cache.db"], [base stringByAppendingString:@"/dbl/cache.db"]);
    try_form("trailing", [base stringByAppendingString:@"/trail/"], [base stringByAppendingString:@"/trail"]);
    try_form("trailing-dir-exists", [base stringByAppendingString:@"/trail2/"], [base stringByAppendingString:@"/trail2"]);
    try_form("dot", [base stringByAppendingString:@"/dot/./cache.db"], [base stringByAppendingString:@"/dot/cache.db"]);
}

/* Relative paths: under the user cache folder (stringByAppendingPathComponent:), the file's parent created. */
static void relative_path(void) {
    NSString *base = user_cache_dir();
    if (!base) {
        wm_val("cache-dir", "missing");
        return;
    }
    NSString *unique = [NSString stringWithFormat:@"wm-probe-g8-%d-%u", getpid(), arc4random()];
    NSString *top = [base stringByAppendingPathComponent:unique];
    struct {
        const char *label;
        NSString *rel, *at;
    } forms[] = {
        {"nested", [unique stringByAppendingString:@"/a/b/cache.db"], [top stringByAppendingString:@"/a/b/cache.db"]},
        {"doubled", [unique stringByAppendingString:@"/dbl//cache.db"], [top stringByAppendingString:@"/dbl/cache.db"]},
        {"trailing", [unique stringByAppendingString:@"/trail/"], [top stringByAppendingString:@"/trail"]},
        {"flat", [unique stringByAppendingString:@"-flat.db"], [top stringByAppendingString:@"-flat.db"]},
    };
    for (unsigned i = 0; i < sizeof forms / sizeof *forms; i++) try_form(forms[i].label, forms[i].rel, forms[i].at);
    removefile(top.fileSystemRepresentation, NULL, REMOVEFILE_RECURSIVE);
    for (NSString *e in @[ @"-flat.db", @"-flat.db-lock", @"-flat.db-wal", @"-flat.db-shm" ])
        unlink([top stringByAppendingString:e].fileSystemRepresentation);
    wm_val("cleaned", "%s", yesno(!exists(top)));
}

/* Paths and files that make 115 or 117 answer nil (the C logs a line and returns nil). */
static void bad_path(void) {
    NSString *base = tmp("bad");
    [[NSFileManager defaultManager] createDirectoryAtPath:base withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *notdb = [base stringByAppendingString:@"/notdb.db"];
    [@"this is not a database file at all, just text" writeToFile:notdb atomically:NO encoding:NSUTF8StringEncoding error:nil];
    NSString *dirdb = [base stringByAppendingString:@"/dir.db"];
    mkdir(dirdb.fileSystemRepresentation, 0755);
    NSString *empty = [base stringByAppendingString:@"/empty.db"];
    sql_exec(empty, "CREATE TABLE unrelated(x)");
    NSString *ro = [base stringByAppendingString:@"/ro.db"];
    {
        uint64_t w = open_cache(117, ro, 1, false);
        if (w) {
            put(w, key_for(1), value_for(1));
            [(id)w release];
        }
        chmod(ro.fileSystemRepresentation, 0444);
    }
    struct {
        const char *label;
        NSString *path;
        uint64_t version;
    } forms[] = {
        {"under-dev-null", @"/dev/null/x/cache.db", 1},
        {"under-root", @"/wm-probe-no-such-root-folder/x/cache.db", 1},
        {"missing-file", [base stringByAppendingString:@"/missing.db"], 1},
        {"not-a-database", notdb, 1},
        {"directory", dirdb, 1},
        {"no-table", empty, 1},
        {"read-only", ro, 1},
        {"read-only-other-version", ro, 2},
    };
    for (unsigned i = 0; i < sizeof forms / sizeof *forms; i++) {
        wm_line("form=%s", forms[i].label);
        uint64_t r = open_cache(115, forms[i].path, forms[i].version, false);
        if (r) [(id)r release];
        wm_val("exists-after-reader", "%s", yesno(exists(forms[i].path))); /* a reader never creates the file */
        uint64_t w = open_cache(117, forms[i].path, forms[i].version, false);
        if (w) [(id)w release];
    }
    wm_val("ro-tables", "%s", sql_rows(ro, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").UTF8String);
    chmod(ro.fileSystemRepresentation, 0644);
}

/* ---- locking ---- */

struct holder {
    const char *lock;
    unsigned ms;
    dispatch_semaphore_t held;
    volatile int released;
};

static void *hold_lock(void *arg) {
    struct holder *h = arg;
    int fd = open(h->lock, O_RDWR | O_CREAT, 0666);
    flock(fd, LOCK_EX);
    dispatch_semaphore_signal(h->held);
    usleep(h->ms * 1000);
    h->released = 1;
    flock(fd, LOCK_UN);
    close(fd);
    return NULL;
}

/* 117 creates <db>-lock; while another open file holds LOCK_EX on it the open waits for it, and gives the lock up
 * (and closes it) when done. A reader doesn't take the lock. */
static void lock_file(void) {
    NSString *dir = tmp("lock");
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *db = [dir stringByAppendingPathComponent:@"cache.db"];
    NSString *lock = [db stringByAppendingString:@"-lock"];
    uint64_t first = open_cache(117, db, 1, false);
    if (!first) return;
    put(first, key_for(1), value_for(1));
    [(id)first release];
    wm_val("lock-file", "%s", yesno(exists(lock)));
    {
        struct holder h = {.lock = lock.fileSystemRepresentation, .ms = 300, .held = dispatch_semaphore_create(0)};
        pthread_t t;
        pthread_create(&t, NULL, hold_lock, &h);
        dispatch_semaphore_wait(h.held, DISPATCH_TIME_FOREVER);
        uint64_t w = open_cache(117, db, 1, false);
        wm_val("writer-waited", "%s", yesno(h.released)); /* released is set before the unlock the open waited for */
        pthread_join(t, NULL);
        if (w) [(id)w release];
    }
    {
        int fd = open(lock.fileSystemRepresentation, O_RDWR);
        wm_val("lock-free-after", "%s", yesno(!flock(fd, LOCK_EX | LOCK_NB)));
        close(fd);
    }
    {
        struct holder h = {.lock = lock.fileSystemRepresentation, .ms = 800, .held = dispatch_semaphore_create(0)};
        pthread_t t;
        pthread_create(&t, NULL, hold_lock, &h);
        dispatch_semaphore_wait(h.held, DISPATCH_TIME_FOREVER);
        uint64_t r = open_cache(115, db, 1, false);
        wm_val("reader-waited", "%s", yesno(h.released));
        if (r) {
            hit("hit", get(r, key_for(1), false), value_for(1));
            [(id)r release];
        }
        pthread_join(t, NULL);
    }
}

/* ---- 119 ---- */

static NSString *metal_path_before;

static void try_cache_path(const char *label, NSString *path, NSString *created_at) {
    struct unixcall_setmetalcachepath p;
    wm_fill(&p, sizeof p);
    p.path.ptr = path.fileSystemRepresentation;
    wm_line("form=%s", label);
    wm_call(119, &p);
    wm_val("ret", "%llu", (unsigned long long)p.ret_success);
    bool dir = false;
    struct stat sb;
    if (!stat(created_at.fileSystemRepresentation, &sb)) dir = S_ISDIR(sb.st_mode);
    wm_val("folder", "%s", yesno(dir));
    NSString *now = MTLGetShaderCachePath();
    wm_val("metal-has", "%s", !now ? "nil" : [now isEqualToString:path] ? "path" : [now isEqualToString:created_at] ? "normalised-path"
                              : [now isEqualToString:metal_path_before] ? "unchanged" : "other");
}

/* 119 creates the folder itself (not just its parent), sets Metal's shader cache folder and says whether Metal's own
 * getter returns the same string. Isolated: it changes this process's Metal state for good. */
static void metal_cache_path(void) {
    NSString *base = tmp("mcp");
    metal_path_before = [MTLGetShaderCachePath() copy];
    try_cache_path("new", [base stringByAppendingString:@"/new"], [base stringByAppendingString:@"/new"]);
    try_cache_path("deep", [base stringByAppendingString:@"/deep/a/b"], [base stringByAppendingString:@"/deep/a/b"]);
    try_cache_path("existing", [base stringByAppendingString:@"/new"], [base stringByAppendingString:@"/new"]);
    try_cache_path("trailing", [base stringByAppendingString:@"/trail/"], [base stringByAppendingString:@"/trail"]);
    try_cache_path("doubled", [base stringByAppendingString:@"//dbl//x"], [base stringByAppendingString:@"/dbl/x"]);
    NSString *cache = user_cache_dir();
    if (cache) {
        NSString *unique = [NSString stringWithFormat:@"wm-probe-g8-mcp-%d-%u", getpid(), arc4random()];
        NSString *at = [cache stringByAppendingPathComponent:unique];
        try_cache_path("relative", unique, at);
        removefile(at.fileSystemRepresentation, NULL, REMOVEFILE_RECURSIVE);
        try_cache_path("relative-trailing", [unique stringByAppendingString:@"/"], at);
        removefile(at.fileSystemRepresentation, NULL, REMOVEFILE_RECURSIVE);
    }
}

/* ---- the classes, edges, nil, leaks ---- */

/* The native image registers DXMTNativeCacheReader and DXMTNativeCacheWriter (a Wine image beside it keeps CacheReader
 * and CacheWriter): the objects it makes are of those. */
static void class_names(void) {
    NSString *db = tmp("classes/cache.db");
    uint64_t w = open_cache(117, db, 1, true);
    uint64_t r = w ? open_cache(115, db, 1, true) : 0;
    (void)r;
    classes();
}

/* Keys and values at the edges: an empty key, a key with NULs, a 4 KiB key, a 1.5 MiB value, an empty value (the C
 * binds what dispatch_data_create_map gives, so what the file holds is the C's). */
static void edge_keys(void) {
    NSString *db = tmp("edges/cache.db");
    uint64_t w = open_cache(117, db, 1, false);
    if (!w) return;
    uint8_t nul[8] = {0, 1, 0, 2, 0, 3, 0, 4};
    NSMutableData *bigkey = [NSMutableData dataWithLength:4096];
    memset(bigkey.mutableBytes, 0x6B, 4096);
    NSMutableData *bigvalue = [NSMutableData dataWithLength:1536 * 1024];
    for (size_t i = 0; i < bigvalue.length; i++) ((uint8_t *)bigvalue.mutableBytes)[i] = (uint8_t)(i * 13);
    NSData *keys[] = {[NSData data], [NSData dataWithBytes:nul length:8], bigkey, key_for(5), key_for(6)};
    NSData *values[] = {value_for(1), value_for(2), value_for(3), bigvalue, [NSData data]};
    for (unsigned i = 0; i < 5; i++) put(w, keys[i], values[i]);
    [(id)w release];
    wm_val("rows", "%s", sql_rows(db, "SELECT typeof(key), length(key), typeof(value), length(value) FROM cache_1 ORDER BY rowid").UTF8String);
    uint64_t r = open_cache(115, db, 1, false);
    for (unsigned i = 0; i < 5; i++) hit("hit", get(r, keys[i], false), values[i]);
    [(id)r release];
}

/* 116 and 118 on handle 0 are nil sends: 116 writes 0, 118 nothing. */
static void nil_handle(void) {
    struct unixcall_cache_get g;
    wm_fill(&g, sizeof g);
    g.cache = 0;
    g.key.ptr = "key-0001";
    g.key_length = 8;
    wm_call(116, &g);
    wm_val("ret_data", "%s", !g.ret_data ? "0" : g.ret_data == WM_FILL64 ? "fill" : "nonnull");
    NSData *v = value_for(1);
    dispatch_data_t d = dd_of(v);
    struct unixcall_cache_set s;
    wm_fill(&s, sizeof s);
    s.cache = 0;
    s.key.ptr = "key-0001";
    s.key_length = 8;
    s.value_data = (uint64_t)d;
    wm_call(118, &s);
    [(id)d release];
}

/* Open, write, read and close, over and over: nothing is left behind. */
static void heap_growth(void) {
    NSString *db = tmp("growth/cache.db");
    wm_heap_growth(^{
      uint64_t w = open_cache(117, db, 1, false);
      if (!w) return;
      put(w, key_for(1), value_for(1));
      uint64_t r = open_cache(115, db, 1, false);
      if (r) {
          get(r, key_for(1), false);
          [(id)r release];
      }
      [(id)w release];
    }, 40);
}

const struct wm_case wm_cases_g8[] = {
    {"g8", 117, "writer-reader", 0, writer_reader},
    {"g8", 116, "cross-read", 0, cross_read},
    {"g8", 118, "replace", 0, replace},
    {"g8", 117, "versions", 0, concurrent_versions},
    {"g8", 117, "version-migrate", 0, version_migrate},
    {"g8", 117, "db-bytes", WM_FULL_ONLY, db_bytes},
    {"g8", 117, "path-forms", 0, path_forms},
    {"g8", 117, "relative-path", 0, relative_path},
    {"g8", 117, "lock-file", 0, lock_file},
    {"g8", 115, "bad-path", 0, bad_path},
    {"g8", 119, "metal-cache-path", WM_ISOLATE, metal_cache_path},
    {"g8", 117, "class-names", WM_NATIVE, class_names},
    {"g8", 118, "edge-keys", 0, edge_keys},
    {"g8", 116, "nil", 0, nil_handle},
    {"g8", 117, "heap-growth", 0, heap_growth},
    {0},
};
