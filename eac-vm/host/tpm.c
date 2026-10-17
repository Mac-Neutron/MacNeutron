/* The guest's TPM 2.0 (macneutron-vm --tpm <dir>; research README §6 and §13, EAC v2): libkrun's TpmDevice, a TIS
 * interface that Linux's built-in tpm_tis binds to, run by libtpms (eac-vm/libtpms) with its default-v1 profile.
 *
 * Its state is kept the way Parallels Desktop keeps its virtual TPM's (kb.parallels.com/en/122702): files in its
 * folder, encrypted (AES-256-GCM, each file's name authenticated with it; Parallels uses AES-128) under a key that
 * lives in the login keychain, never in the folder. The keychain item's account is the folder's random id (<dir>/id),
 * so the key follows the TPM, not the game, and a copy on the same Mac keeps working. A folder whose key isn't in
 * this Mac's keychain (copied from another Mac), or whose files don't decrypt, moves to <dir>/backup-<id>/ and the TPM
 * starts afresh, a new identity: Parallels 18's way. That is decided once, before libtpms starts. Anything else (a
 * keychain that is locked or says no, a file that can't be read just now) fails the start instead of losing the
 * state. */
#include "tpm.h"
#include <Security/Security.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libtpms/tpm_error.h>
#include <libtpms/tpm_library.h>
#include <limits.h>
#include <openssl/evp.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

extern char **environ;

const char *mn_tpm_service = "MacNeutron vTPM";

static struct {
    char dir[PATH_MAX];
    char id[37];
    uint8_t key[32];
    uint8_t locality; /* the command's, for libtpms's tpm_io_getlocality (one command at a time) */
    int lock;         /* <dir>/.lock, held while the TPM runs: one VM per TPM */
    int told;         /* that it's a new TPM (libtpms looks for its state twice) */
} tpm = {.lock = -1};

static void path(char *out, const char *name) { snprintf(out, PATH_MAX, "%s/%s", tpm.dir, name); }

/* The key: a generic password of mn_tpm_service, account tpm.id. 0, 1 if there's none, -1 if the keychain refused. */
static CFMutableDictionaryRef item(void) {
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = CFStringCreateWithCString(NULL, mn_tpm_service, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithCString(NULL, tpm.id, kCFStringEncodingUTF8);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(service), CFRelease(account);
    return q;
}

/* macOS asks for the login password before it hands the key to a macneutron-vm the key's item doesn't trust yet (one
 * signed otherwise: a development build, or another Mac's copy), which leaves the read waiting on the player. A read
 * that hasn't answered in half a second gets a toast that says why (MN_TOAST: the launcher's `macneutron toast`),
 * until it does. */
struct waiting { pthread_mutex_t m; pthread_cond_t c; int done; };

static void *say_why(void *arg) {
    struct waiting *w = arg;
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += 500000000;
    if (until.tv_nsec >= 1000000000) until.tv_sec++, until.tv_nsec -= 1000000000;
    pthread_mutex_lock(&w->m);
    while (!w->done && pthread_cond_timedwait(&w->c, &w->m, &until) == 0) {}
    int waiting = !w->done;
    pthread_mutex_unlock(&w->m);
    const char *toast = getenv("MN_TOAST");
    int in[2];
    if (!waiting || !toast || !*toast || pipe(in)) return NULL;
    char *argv[] = {(char *)toast, "toast", "Why macOS asks for your password",
                    "MacNeutron keeps this game's virtual TPM, the security chip Windows and anti-cheat look for, "
                    "locked with a key in your login keychain. Enter your password and choose Always Allow: it won't "
                    "ask again.",
                    NULL};
    posix_spawn_file_actions_t fa;
    posix_spawnattr_t at;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, in[0], 0); /* only its stdin: none of the VM's descriptors (its locks) */
    posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT);
    pid_t pid;
    int spawned = posix_spawn(&pid, toast, &fa, &at, argv, environ) == 0;
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    close(in[0]);
    pthread_mutex_lock(&w->m);
    while (!w->done) pthread_cond_wait(&w->c, &w->m);
    pthread_mutex_unlock(&w->m);
    close(in[1]); /* the toast goes */
    if (spawned) waitpid(pid, NULL, 0);
    return NULL;
}

static OSStatus copy_saying_why(CFDictionaryRef q, CFTypeRef *v) {
    struct waiting w = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0};
    pthread_t t;
    int watching = pthread_create(&t, NULL, say_why, &w) == 0;
    OSStatus rc = SecItemCopyMatching(q, v);
    pthread_mutex_lock(&w.m);
    w.done = 1;
    pthread_cond_broadcast(&w.c);
    pthread_mutex_unlock(&w.m);
    if (watching) pthread_join(t, NULL);
    return rc;
}

static int key_get(void) {
    CFMutableDictionaryRef q = item();
    CFTypeRef v = NULL;
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    OSStatus rc = copy_saying_why(q, &v);
    CFRelease(q);
    if (rc == errSecItemNotFound) return 1;
    long got = rc == errSecSuccess ? (long)CFDataGetLength(v) : 0;
    int ok = got == sizeof tpm.key;
    if (ok) CFDataGetBytes(v, CFRangeMake(0, sizeof tpm.key), tpm.key);
    else if (rc == errSecSuccess) fprintf(stderr, "mn-tpm: this TPM's key in the keychain is %ld bytes, not 32\n", got);
    else fprintf(stderr, "mn-tpm: the keychain refused the TPM's key (%d)\n", (int)rc);
    if (v) CFRelease(v);
    return ok ? 0 : -1;
}

static int key_add(void) {
    CFMutableDictionaryRef q = item();
    CFDataRef d = CFDataCreate(NULL, tpm.key, sizeof tpm.key);
    CFDictionarySetValue(q, kSecValueData, d);
    CFDictionarySetValue(q, kSecAttrLabel, CFSTR("MacNeutron virtual TPM"));
    OSStatus rc = SecItemAdd(q, NULL);
    CFRelease(d), CFRelease(q);
    if (rc != errSecSuccess) fprintf(stderr, "mn-tpm: the keychain refused a new key (%d)\n", (int)rc);
    return rc == errSecSuccess ? 0 : -1;
}

/* The whole file, written beside it and renamed over it: a crash leaves the old one or the new one. */
static int write_file(const char *name, const uint8_t *data, size_t n) {
    char p[PATH_MAX], tmp[PATH_MAX];
    path(p, name), snprintf(tmp, sizeof tmp, "%s.tmp", p);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    /* F_BARRIERFSYNC: the data reaches the disk before the rename does (fsync alone doesn't order them on macOS). */
    int ok = write(fd, data, n) == (ssize_t)n && (fcntl(fd, F_BARRIERFSYNC) == 0 || fsync(fd) == 0);
    close(fd);
    if (ok && rename(tmp, p) == 0) return 0;
    unlink(tmp);
    return -1;
}

/* A new identity: a new id, a new key in the keychain. */
static int fresh(void) {
    uuid_t u;
    uuid_generate_random(u), uuid_unparse_lower(u, tpm.id);
    arc4random_buf(tpm.key, sizeof tpm.key);
    if (key_add() != 0) return -1;
    return write_file("id", (const uint8_t *)tpm.id, strlen(tpm.id));
}

/* Everything but earlier backups into backup-<id>/ (a new id when there's none; -2, -3... if that one is taken), for a
 * fresh TPM to start from. */
static int backup(void) {
    char to[PATH_MAX], from[PATH_MAX], name[64];
    if (!tpm.id[0]) {
        uuid_t u;
        uuid_generate_random(u), uuid_unparse_lower(u, tpm.id);
    }
    snprintf(name, sizeof name, "backup-%s", tpm.id), path(to, name);
    for (int i = 2; mkdir(to, 0700) != 0; i++) {
        if (errno != EEXIST || i > 99) return -1;
        snprintf(name, sizeof name, "backup-%s-%d", tpm.id, i), path(to, name);
    }
    DIR *d = opendir(tpm.dir);
    if (!d) return -1;
    int rc = 0;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.' || !strncmp(e->d_name, "backup-", 7)) continue;
        char dst[PATH_MAX];
        path(from, e->d_name), snprintf(dst, sizeof dst, "%s/%s", to, e->d_name);
        if (rename(from, dst) != 0) rc = -1;
    }
    closedir(d);
    fprintf(stderr, "mn-tpm: a new TPM; the old one's files are in %s\n", to);
    return rc;
}

/* AES-256-GCM, the file's name as associated data. A file is iv (12) | ciphertext | tag (16). */
static int gcm(int enc, const char *name, const uint8_t *in, size_t n, uint8_t *out, const uint8_t *iv, uint8_t *tag) {
    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    int len, ok = c && EVP_CipherInit_ex(c, EVP_aes_256_gcm(), NULL, tpm.key, iv, enc) &&
                  EVP_CipherUpdate(c, NULL, &len, (const uint8_t *)name, (int)strlen(name)) &&
                  (enc || EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag)) &&
                  EVP_CipherUpdate(c, out, &len, in, (int)n) && EVP_CipherFinal_ex(c, out + len, &len) &&
                  (!enc || EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, tag));
    EVP_CIPHER_CTX_free(c);
    return ok ? 0 : -1;
}

/* A sealed file's plaintext (malloc'd). 0, 1 if there's none, 2 if it isn't this key's (or is cut short), -1 if it
 * can't be read. */
static int unseal(const char *name, unsigned char **data, uint32_t *length) {
    char p[PATH_MAX];
    path(p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return errno == ENOENT ? 1 : -1;
    struct stat sb;
    uint8_t *file = NULL;
    int rc = -1;
    if (fstat(fileno(f), &sb) == 0 && (file = malloc(sb.st_size + 1)) &&
        fread(file, 1, sb.st_size, f) == (size_t)sb.st_size) {
        size_t n = sb.st_size;
        rc = n < 28 ? 2 : (*data = malloc(n - 28 + 1)) ? 0 : -1;
        if (rc == 0 && gcm(0, name, file + 12, n - 28, *data, file, file + n - 16) != 0)
            free(*data), *data = NULL, rc = 2;
        if (rc == 0) *length = (uint32_t)(n - 28);
    }
    fclose(f), free(file);
    return rc;
}

/* Each of libtpms's files opens with the key: 0, or a fresh TPM if one doesn't (-1 if one can't be read). */
static int check_files(void) {
    DIR *d = opendir(tpm.dir);
    if (!d) return -1;
    int rc = 0;
    for (struct dirent *e; rc == 0 && (e = readdir(d));) {
        size_t n = strlen(e->d_name);
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "id") || !strncmp(e->d_name, "backup-", 7) ||
            (n > 4 && !strcmp(e->d_name + n - 4, ".tmp")))
            continue;
        unsigned char *data = NULL;
        uint32_t len;
        rc = unseal(e->d_name, &data, &len);
        free(data);
        if (rc == 2) fprintf(stderr, "mn-tpm: %s doesn't open with this TPM's key\n", e->d_name);
        if (rc == -1) fprintf(stderr, "mn-tpm: can't read %s/%s\n", tpm.dir, e->d_name);
    }
    closedir(d);
    if (rc == 2) return backup() == 0 ? fresh() : -1;
    return rc < 0 ? -1 : 0;
}

static int open_state(const char *dir) {
    snprintf(tpm.dir, sizeof tpm.dir, "%s", dir), tpm.id[0] = 0, tpm.told = 0;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    char p[PATH_MAX];
    path(p, ".lock");
    if ((tpm.lock = open(p, O_RDWR | O_CREAT, 0600)) < 0 || flock(tpm.lock, LOCK_EX | LOCK_NB) != 0)
        return fprintf(stderr, "mn-tpm: %s is in use by another VM\n", dir), -1;
    path(p, "id");
    FILE *f = fopen(p, "r");
    if (!f && errno != ENOENT) return fprintf(stderr, "mn-tpm: can't read %s\n", p), -1;
    if (f) {
        int ok = fscanf(f, "%36s", tpm.id) == 1;
        fclose(f);
        if (ok) {
            int k = key_get();
            if (k < 0) return -1;
            if (k == 0) return check_files();
        }
    } else {
        int empty = 1;
        DIR *d = opendir(dir);
        for (struct dirent *e; d && (e = readdir(d));)
            if (e->d_name[0] != '.' && strncmp(e->d_name, "backup-", 7)) empty = 0;
        if (d) closedir(d);
        if (empty) return fresh();
    }
    return backup() == 0 ? fresh() : -1;
}

/* libtpms's storage (TPMLIB_RegisterCallbacks): "permall" is the TPM's NV, the rest its volatile state. */
static TPM_RESULT nv_init(void) { return TPM_SUCCESS; }

static TPM_RESULT nv_load(unsigned char **data, uint32_t *length, uint32_t tpm_number, const char *name) {
    (void)tpm_number;
    *data = NULL, *length = 0;
    switch (unseal(name, data, length)) { /* checked before the start: a failure now is the disk's, not the key's */
    case 0: return TPM_SUCCESS;
    case 1: /* none yet: the first start */
        if (!strcmp(name, "permall") && !tpm.told++) fprintf(stderr, "mn-tpm: no saved state in %s: a new TPM\n", tpm.dir);
        return TPM_RETRY;
    default: fprintf(stderr, "mn-tpm: can't read %s now\n", name); return TPM_FAIL;
    }
}

static TPM_RESULT nv_store(const unsigned char *data, uint32_t length, uint32_t tpm_number, const char *name) {
    (void)tpm_number;
    uint8_t *file = malloc(length + 28);
    int ok = file != NULL;
    if (ok) arc4random_buf(file, 12);
    ok = ok && gcm(1, name, data, length, file + 12, file, file + 12 + length) == 0 &&
         write_file(name, file, length + 28) == 0;
    free(file);
    if (!ok) fprintf(stderr, "mn-tpm: couldn't save %s\n", name);
    return ok ? TPM_SUCCESS : TPM_FAIL;
}

static TPM_RESULT nv_delete(uint32_t tpm_number, const char *name, TPM_BOOL must_exist) {
    (void)tpm_number;
    char p[PATH_MAX];
    path(p, name);
    return unlink(p) == 0 || (errno == ENOENT && !must_exist) ? TPM_SUCCESS : TPM_FAIL;
}

static TPM_RESULT io_init(void) { return TPM_SUCCESS; }

static TPM_RESULT io_locality(TPM_MODIFIER_INDICATOR *locality, uint32_t tpm_number) {
    (void)tpm_number;
    *locality = tpm.locality;
    return TPM_SUCCESS;
}

static TPM_RESULT io_presence(TPM_BOOL *presence, uint32_t tpm_number) {
    (void)tpm_number;
    *presence = 0;
    return TPM_SUCCESS;
}

static void unlock(void) {
    if (tpm.lock >= 0) close(tpm.lock);
    tpm.lock = -1;
}

int mn_tpm_start(const char *dir) {
    static struct libtpms_callbacks cb = {sizeof cb, nv_init, nv_load, nv_store, nv_delete, io_init, io_locality,
                                          io_presence};
    if (open_state(dir) == 0) {
        if (TPMLIB_ChooseTPMVersion(TPMLIB_TPM_VERSION_2) == TPM_SUCCESS &&
            TPMLIB_RegisterCallbacks(&cb) == TPM_SUCCESS &&
            TPMLIB_SetProfile("{\"Name\":\"default-v1\"}") == TPM_SUCCESS && TPMLIB_MainInit() == TPM_SUCCESS)
            return 0;
        fprintf(stderr, "mn-tpm: libtpms didn't start\n");
        TPMLIB_Terminate();
    }
    unlock();
    return -1;
}

void mn_tpm_stop(void) { TPMLIB_Terminate(), unlock(); }

int32_t mn_tpm_execute(void *userdata, uint8_t locality, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap) {
    static unsigned char *buf; /* libtpms's, grown as it needs */
    static uint32_t size;
    static unsigned char command[4096]; /* libtpms decrypts a command's parameters in place: not in libkrun's buffer */
    uint32_t n = 0;
    (void)userdata;
    if (len > sizeof command) return -1;
    memcpy(command, cmd, len);
    tpm.locality = locality;
    if (TPMLIB_Process(&buf, &n, &size, command, (uint32_t)len) != TPM_SUCCESS || n > cap) return -1;
    memcpy(resp, buf, n);
    return (int32_t)n;
}

KrunTpmDevice mn_tpm_device(KrunError *err) {
    static const struct krun_tpm_backend backend = {NULL, mn_tpm_execute};
    return krun_tpm_device_new(&backend, sizeof backend, err);
}
