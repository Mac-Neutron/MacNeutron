/* The guest's TPM 2.0 (macneutron-vm --tpm <dir>; research README §6 and §13, EAC v2): libkrun's TpmDevice, a TIS
 * interface that Linux's built-in tpm_tis binds to, run by libtpms (eac-vm/libtpms) with its default-v1 profile.
 *
 * Its state is kept the way Parallels Desktop keeps its virtual TPM's (kb.parallels.com/en/122702): files in its folder,
 * encrypted (AES-256-GCM, each file's name authenticated with it; Parallels uses AES-128) under a key that lives in the
 * login keychain, never in the folder. The keychain item's account is the folder's random id (<dir>/id), so the key
 * follows the TPM, not the game, and a copy on the same Mac keeps working. A folder whose key isn't in this Mac's
 * keychain (copied from another Mac), or whose files don't decrypt, moves to <dir>/backup-<id>/ and the TPM starts
 * afresh, a new identity: Parallels 18's way. Any other keychain failure (locked, denied) fails the start instead of
 * losing the state. */
#include "tpm.h"
#include <Security/Security.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libtpms/tpm_error.h>
#include <libtpms/tpm_library.h>
#include <limits.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uuid/uuid.h>

const char *mn_tpm_service = "MacNeutron vTPM";

static struct {
    char dir[PATH_MAX];
    char id[37];
    uint8_t key[32];
    uint8_t locality; /* the command's, for libtpms's tpm_io_getlocality (one command at a time) */
} tpm;

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

static int key_get(void) {
    CFMutableDictionaryRef q = item();
    CFTypeRef v = NULL;
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    OSStatus rc = SecItemCopyMatching(q, &v);
    CFRelease(q);
    if (rc == errSecItemNotFound) return 1;
    int ok = rc == errSecSuccess && CFDataGetLength(v) == sizeof tpm.key;
    if (ok) CFDataGetBytes(v, CFRangeMake(0, sizeof tpm.key), tpm.key);
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
    int ok = write(fd, data, n) == (ssize_t)n && fsync(fd) == 0;
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

/* Everything but earlier backups into backup-<id>/ (a new id when there's none), for a fresh TPM to start from. */
static int backup(void) {
    char to[PATH_MAX], from[PATH_MAX], name[64];
    if (!tpm.id[0]) {
        uuid_t u;
        uuid_generate_random(u), uuid_unparse_lower(u, tpm.id);
    }
    snprintf(name, sizeof name, "backup-%s", tpm.id), path(to, name);
    if (mkdir(to, 0700) != 0 && errno != EEXIST) return -1;
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

static int open_state(const char *dir) {
    snprintf(tpm.dir, sizeof tpm.dir, "%s", dir), tpm.id[0] = 0;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return -1;
    char p[PATH_MAX];
    path(p, "id");
    FILE *f = fopen(p, "r");
    if (f) {
        int ok = fscanf(f, "%36s", tpm.id) == 1;
        fclose(f);
        if (ok) {
            int k = key_get();
            if (k <= 0) return k;
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

/* libtpms's storage (TPMLIB_RegisterCallbacks): "permall" is the TPM's NV, the rest its volatile state. */
static TPM_RESULT nv_init(void) { return TPM_SUCCESS; }

static TPM_RESULT nv_load(unsigned char **data, uint32_t *length, uint32_t tpm_number, const char *name) {
    (void)tpm_number;
    *data = NULL, *length = 0;
    char p[PATH_MAX];
    path(p, name);
    FILE *f = fopen(p, "rb");
    if (!f) return errno == ENOENT ? TPM_RETRY : TPM_FAIL; /* TPM_RETRY: none yet, the first start */
    uint8_t *file = NULL;
    long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (n >= 28 && fseek(f, 0, SEEK_SET) == 0 && (file = malloc(n)) && fread(file, 1, n, f) == (size_t)n &&
        (*data = malloc(n - 28)) && gcm(0, name, file + 12, n - 28, *data, file, file + n - 16) == 0) {
        *length = (uint32_t)(n - 28);
        fclose(f), free(file);
        return TPM_SUCCESS;
    }
    fclose(f), free(file), free(*data), *data = NULL;
    fprintf(stderr, "mn-tpm: %s doesn't open with this TPM's key\n", name);
    return backup() == 0 && fresh() == 0 ? TPM_RETRY : TPM_FAIL;
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

int mn_tpm_start(const char *dir) {
    static struct libtpms_callbacks cb = {sizeof cb, nv_init, nv_load, nv_store, nv_delete, io_init, io_locality,
                                          io_presence};
    if (open_state(dir) != 0) return -1;
    if (TPMLIB_ChooseTPMVersion(TPMLIB_TPM_VERSION_2) == TPM_SUCCESS && TPMLIB_RegisterCallbacks(&cb) == TPM_SUCCESS &&
        TPMLIB_SetProfile("{\"Name\":\"default-v1\"}") == TPM_SUCCESS && TPMLIB_MainInit() == TPM_SUCCESS)
        return 0;
    fprintf(stderr, "mn-tpm: libtpms didn't start\n");
    return -1;
}

void mn_tpm_stop(void) { TPMLIB_Terminate(); }

int32_t mn_tpm_execute(void *userdata, uint8_t locality, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap) {
    static unsigned char *buf; /* libtpms's, grown as it needs */
    static uint32_t size;
    uint32_t n = 0;
    (void)userdata;
    tpm.locality = locality;
    if (TPMLIB_Process(&buf, &n, &size, (unsigned char *)cmd, (uint32_t)len) != TPM_SUCCESS || n > cap) return -1;
    memcpy(resp, buf, n);
    return (int32_t)n;
}

KrunTpmDevice mn_tpm_device(KrunError *err) {
    static const struct krun_tpm_backend backend = {NULL, mn_tpm_execute};
    return krun_tpm_device_new(&backend, sizeof backend, err);
}
