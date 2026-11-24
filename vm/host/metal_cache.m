// Ensures Metal's compiler cache (com.apple.metalfe) lives in a writable tree before Metal initializes its
// cache path via dispatch_once. If the Darwin user cache directory is unwritable (e.g. root-owned files,
// UF_IMMUTABLE flags, symlinks, or restrictive ACLs), redirects Metal via MTLSetShaderCachePath to a writable
// fallback directory.
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <fts.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool mkdir_p(const char *path, mode_t mode) {
    if (!path || path[0] == '\0') {
        return false;
    }
    char buf[PATH_MAX];
    size_t len = strlcpy(buf, path, sizeof(buf));
    if (len >= sizeof(buf)) {
        return false;
    }
    while (len > 1 && buf[len - 1] == '/') {
        buf[--len] = '\0';
    }
    for (char *p = buf + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buf, mode) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    if (mkdir(buf, mode) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat st;
    if (lstat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return false;
    }
    return true;
}

static bool probe_dir(const char *dir) {
    char probe[PATH_MAX];
    int n = snprintf(probe, sizeof(probe), "%s/.mn-write-probe-%d-%u", dir, (int)getpid(), arc4random());
    if (n < 0 || (size_t)n >= sizeof(probe)) {
        return false;
    }
    int fd = open(probe, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return false;
    }
    char byte = 0;
    ssize_t w = write(fd, &byte, 1);
    close(fd);
    int unlinked = unlink(probe);
    return w == 1 && unlinked == 0;
}

static bool check_writable_tree(const char *root) {
    if (!mkdir_p(root, 0700)) {
        return false;
    }
    char *paths[2] = { (char *)root, NULL };
    FTS *fts = fts_open(paths, FTS_PHYSICAL | FTS_NOCHDIR, NULL);
    if (!fts) {
        return false;
    }
    bool ok = true;
    FTSENT *ent = NULL;
    while (errno = 0, (ent = fts_read(fts)) != NULL) {
        switch (ent->fts_info) {
            case FTS_SL:
            case FTS_SLNONE:
            case FTS_ERR:
            case FTS_DNR:
            case FTS_NS:
                ok = false;
                break;
            case FTS_D:
                if (!probe_dir(ent->fts_path)) {
                    ok = false;
                }
                break;
            case FTS_F: {
                int fd = open(ent->fts_path, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
                if (fd < 0) {
                    ok = false;
                } else {
                    close(fd);
                }
                break;
            }
            default:
                break;
        }
        if (!ok) {
            break;
        }
    }
    if (ent == NULL && errno != 0) {
        ok = false;
    }
    fts_close(fts);
    return ok;
}

static bool build_primary_path(char *out, size_t cap) {
    char cache_dir[PATH_MAX];
    size_t n = confstr(_CS_DARWIN_USER_CACHE_DIR, cache_dir, sizeof(cache_dir));
    if (n <= 1 || n > sizeof(cache_dir)) {
        return false;
    }
    size_t len = strlen(cache_dir);
    while (len > 1 && cache_dir[len - 1] == '/') {
        cache_dir[--len] = '\0';
    }
    NSString *bundle_id = [[NSBundle mainBundle] bundleIdentifier];
    const char *bid = (bundle_id != nil && [bundle_id length] > 0)
        ? [bundle_id UTF8String]
        : "io.github.chadouming.MacNeutron.vm";
    if (!bid || bid[0] == '\0') {
        bid = "io.github.chadouming.MacNeutron.vm";
    }
    int written = snprintf(out, cap, "%s/%s/com.apple.metalfe", cache_dir, bid);
    return written > 0 && (size_t)written < cap;
}

static bool build_fallback_path(char *out, size_t cap) {
    const char *env_fb = getenv("MN_METAL_CACHE_FALLBACK");
    if (env_fb && env_fb[0] == '/') {
        return strlcpy(out, env_fb, cap) < cap;
    }
    const char *home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return false;
    }
    int written = snprintf(out, cap, "%s/Library/Caches/MacNeutron/vm/metal-compiler", home);
    return written > 0 && (size_t)written < cap;
}

void mn_prepare_metal_shader_cache(void) {
    @autoreleasepool {
        char primary[PATH_MAX] = {0};
        bool has_primary = build_primary_path(primary, sizeof(primary));
        if (has_primary && check_writable_tree(primary)) {
            return;
        }

        char fallback[PATH_MAX] = {0};
        bool has_fallback = build_fallback_path(fallback, sizeof(fallback));
        if (!has_fallback || !check_writable_tree(fallback)) {
            fprintf(stderr,
                    "macneutron-vm: metalfe cache %s and fallback %s both unwritable\n",
                    has_primary ? primary : "(unavailable)",
                    has_fallback ? fallback : "(unavailable)");
            return;
        }

        typedef void (*mtl_set_cache_fn)(NSString *);
        void *sym = dlsym(RTLD_DEFAULT, "MTLSetShaderCachePath");
        if (sym == NULL) {
            void *metal = dlopen("/System/Library/Frameworks/Metal.framework/Metal", RTLD_LAZY);
            if (metal != NULL) {
                sym = dlsym(metal, "MTLSetShaderCachePath");
            }
        }
        if (sym != NULL) {
            mtl_set_cache_fn fn = (mtl_set_cache_fn)sym;
            NSString *ns_fallback = [NSString stringWithUTF8String:fallback];
            if (ns_fallback != nil) {
                fn(ns_fallback);
                fprintf(stderr, "macneutron-vm: metalfe cache %s unwritable, redirected to %s\n",
                        has_primary ? primary : "(unavailable)", fallback);
            }
        }
    }
}
