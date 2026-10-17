/* tpm.c's engine and state on the host, without a VM (eac-vm/tests/tpm-engine.sh): the same TPM across restarts (its
 * owner seed, seen as the same primary key), its files sealed, and a fresh TPM with the old files kept when they don't
 * open or their key isn't in the keychain. Keys go under their own keychain service, removed at the end.
 * tests/tpm-engine.sh builds tpm.c with SecItemCopyMatching as keychain_copy below, for a keychain that says no, or
 * waits as one does on macOS's password dialog.
 * Prints "tpm-engine: ok" or the first failure. */
#include "tpm.h"
#include <Security/Security.h>
#include <fcntl.h>
#include <libtpms/tpm_error.h>
#include <libtpms/tpm_library.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *dir;
static int failed;
#define CHECK(c, ...) \
    do { if (!(c)) { printf("tpm-engine: FAIL " __VA_ARGS__), printf("\n"), failed = 1; } } while (0)

static int refuse; /* the keychain says no, as a locked one or a "Deny" would */
static int slow;   /* the keychain waits, as while macOS asks for the password */
OSStatus keychain_copy(CFDictionaryRef query, CFTypeRef *result) {
    if (slow) usleep(1500000);
    return refuse ? errSecInteractionNotAllowed : SecItemCopyMatching(query, result);
}

static void clear_keys(void) {
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    CFStringRef s = CFStringCreateWithCString(NULL, mn_tpm_service, kCFStringEncodingUTF8);
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword), CFDictionarySetValue(q, kSecAttrService, s);
    while (SecItemDelete(q) == errSecSuccess) {}
    CFRelease(s), CFRelease(q);
}

static size_t run(const uint8_t *cmd, size_t len, uint8_t *resp) {
    int32_t n = mn_tpm_execute(NULL, 0, cmd, len, resp, 4096);
    return n < 10 ? 0 : (size_t)n;
}

static unsigned rc(const uint8_t *r) { return (unsigned)r[6] << 24 | r[7] << 16 | r[8] << 8 | r[9]; }

/* Power on: Startup(CLEAR), then an ECC P-256 storage primary under the owner hierarchy, whose x coordinate comes
 * from the owner seed: the same TPM gives the same key. */
static int primary(uint8_t x[32]) {
    static const uint8_t startup[] = {0x80, 0x01, 0, 0, 0, 12, 0, 0, 0x01, 0x44, 0, 0};
    static const uint8_t create[] = {
        0x80, 0x02, 0, 0, 0, 67, 0, 0, 0x01, 0x31, 0x40, 0, 0, 0x01, /* CreatePrimary(TPM_RH_OWNER) */
        0, 0, 0, 9, 0x40, 0, 0, 0x09, 0, 0, 0, 0, 0,                  /* password session, empty */
        0, 4, 0, 0, 0, 0,                                             /* inSensitive */
        0, 26, 0, 0x23, 0, 0x0b, 0, 0x03, 0, 0x72, 0, 0,              /* ECC, SHA-256, restricted decrypt */
        0, 0x06, 0, 0x80, 0, 0x43, 0, 0x10, 0, 0x03, 0, 0x10, 0, 0, 0, 0, /* AES-128-CFB, P-256 */
        0, 0, 0, 0, 0, 0};                                            /* outsideInfo, creationPCR */
    uint8_t r[4096];
    size_t n = run(startup, sizeof startup, r);
    if (!n || rc(r)) return printf("tpm-engine: Startup: %zu bytes, rc 0x%x\n", n, n ? rc(r) : 0), -1;
    n = run(create, sizeof create, r);
    if (!n || rc(r)) return printf("tpm-engine: CreatePrimary: %zu bytes, rc 0x%x\n", n, n ? rc(r) : 0), -1;
    size_t policy = (size_t)r[28] << 8 | r[29], at = 30 + policy + 12; /* outPublic: past its policy and parameters */
    if (at + 34 > n || r[at] != 0 || r[at + 1] != 32) return printf("tpm-engine: CreatePrimary: no x\n"), -1;
    memcpy(x, r + at + 2, 32);
    return 0;
}

static int boot(uint8_t x[32]) {
    int ok = mn_tpm_start(dir) == 0 && primary(x) == 0;
    return ok ? 0 : -1;
}

static char *slurp(const char *name, size_t *n) {
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    char *b = malloc(1 << 20);
    *n = fread(b, 1, 1 << 20, f);
    fclose(f);
    return b;
}

static int exists(const char *name) {
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char tmpl[] = "/tmp/mn-tpm-test.XXXXXX";
    dir = mkdtemp(tmpl);
    mn_tpm_service = "MacNeutron vTPM test";
    clear_keys();
    uint8_t x1[32], x2[32], x3[32], x4[32];
    char id1[64] = "", id3[64] = "", bk[128];
    size_t n;

    /* A new TPM, its NV sealed: the file holds none of libtpms's plaintext. */
    CHECK(boot(x1) == 0, "first start");
    unsigned char *plain = NULL;
    uint32_t plain_len = 0;
    CHECK(TPMLIB_GetState(TPMLIB_STATE_PERMANENT, &plain, &plain_len) == TPM_SUCCESS && plain_len > 64, "GetState");
    char *sealed = slurp("permall", &n), *id = slurp("id", &(size_t){0});
    CHECK(sealed && n == plain_len + 28, "permall: %zu bytes for %u", sealed ? n : 0, plain_len);
    CHECK(sealed && plain && !memmem(sealed, n, plain + 16, 32), "permall holds plaintext");
    if (id) snprintf(id1, sizeof id1, "%.36s", id);
    mn_tpm_stop();

    /* The same TPM after a restart. */
    CHECK(boot(x2) == 0 && !memcmp(x1, x2, 32), "a restart changed the TPM");
    mn_tpm_stop();

    /* A key read that waits (macOS's password dialog): a toast says why (MN_TOAST, the launcher's `macneutron toast`)
     * until it answers. A stand-in toast notes its arguments, and its end when its stdin closes. A quick read: none. */
    char toast_dir[] = "/tmp/mn-tpm-toast.XXXXXX", toast[1100], said[1100], ended[1100], line[512] = "";
    mkdtemp(toast_dir);
    snprintf(toast, sizeof toast, "%s/toast", toast_dir);
    snprintf(said, sizeof said, "%s/said", toast_dir);
    snprintf(ended, sizeof ended, "%s/ended", toast_dir);
    FILE *tf = fopen(toast, "w");
    fprintf(tf, "#!/bin/sh\nprintf '%%s\\n' \"$@\" > '%s'\ncat > /dev/null\n: > '%s'\n", said, ended);
    fclose(tf);
    chmod(toast, 0755);
    setenv("MN_TOAST", toast, 1);
    slow = 1;
    CHECK(boot(x2) == 0 && !memcmp(x1, x2, 32), "a slow keychain changed the TPM");
    slow = 0;
    mn_tpm_stop();
    CHECK(access(ended, F_OK) == 0, "no toast while the keychain waited, or it stayed up");
    tf = fopen(said, "r");
    if (tf) {
        fgets(line, sizeof line, tf); /* "toast", its verb */
        fgets(line, sizeof line, tf); /* the title */
        fclose(tf);
    }
    CHECK(strstr(line, "password") != NULL, "the toast's title doesn't say why: %s", line);
    unlink(said), unlink(ended);
    CHECK(boot(x2) == 0, "a quick restart");
    mn_tpm_stop();
    CHECK(access(said, F_OK) != 0, "a quick keychain showed the toast");
    unsetenv("MN_TOAST");
    unlink(toast), rmdir(toast_dir);

    /* A file that can't be read just now (a passing I/O error): no TPM this time, and nothing moved or replaced. */
    char p[1024];
    for (const char *name = "permall"; name; name = strcmp(name, "permall") ? NULL : "id") {
        snprintf(p, sizeof p, "%s/%s", dir, name);
        chmod(p, 0);
        int started = mn_tpm_start(dir) == 0;
        if (started) mn_tpm_stop();
        chmod(p, 0600);
        CHECK(!started, "the TPM started without its %s", name);
        snprintf(bk, sizeof bk, "backup-%s", id1);
        CHECK(!exists(bk), "an unreadable %s moved the TPM aside", name);
        CHECK(boot(x2) == 0 && !memcmp(x1, x2, 32), "an unreadable %s changed the TPM", name);
        mn_tpm_stop();
    }

    /* A keychain that says no: the same. */
    refuse = 1;
    int started = mn_tpm_start(dir) == 0;
    if (started) mn_tpm_stop();
    refuse = 0;
    CHECK(!started, "the TPM started without its key");
    CHECK(!exists(bk), "a keychain that said no moved the TPM aside");
    CHECK(boot(x2) == 0 && !memcmp(x1, x2, 32), "a keychain that said no changed the TPM");
    mn_tpm_stop();

    /* A TPM another VM runs: refused. */
    snprintf(p, sizeof p, "%s/.lock", dir);
    int held = open(p, O_RDWR | O_CREAT, 0600);
    CHECK(held >= 0 && flock(held, LOCK_EX | LOCK_NB) == 0, "lock");
    started = mn_tpm_start(dir) == 0;
    if (started) mn_tpm_stop();
    close(held);
    CHECK(!started, "a TPM in use started again");

    /* A file that doesn't open: kept in a backup folder that holds nothing yet (backup-<id>-2/: an earlier reset's
     * backup-<id>/ stays as it was), and a new TPM. */
    snprintf(p, sizeof p, "%s/backup-%s", dir, id1);
    mkdir(p, 0700);
    snprintf(p, sizeof p, "%s/backup-%s/permall", dir, id1);
    FILE *old = fopen(p, "w");
    if (old) fputs("an earlier reset's", old), fclose(old);
    snprintf(p, sizeof p, "%s/permall", dir);
    FILE *f = fopen(p, "r+b");
    if (f) {  /* its last byte, the GCM tag's */
        fseek(f, -1, SEEK_END);
        int c = fgetc(f);
        fseek(f, -1, SEEK_END), fputc(c ^ 1, f), fclose(f);
    }
    CHECK(boot(x3) == 0 && memcmp(x1, x3, 32), "a damaged permall kept the TPM");
    snprintf(bk, sizeof bk, "backup-%s-2/permall", id1);
    CHECK(exists(bk), "no %s", bk);
    snprintf(bk, sizeof bk, "backup-%s/permall", id1);
    char *earlier = slurp(bk, &n);
    CHECK(earlier && n == 18 && !memcmp(earlier, "an earlier reset's", 18), "the earlier backup was replaced");
    free(earlier);
    free(id), id = slurp("id", &(size_t){0});
    if (id) snprintf(id3, sizeof id3, "%.36s", id);
    CHECK(id && strcmp(id1, id3), "the new TPM kept the old id");
    mn_tpm_stop();

    /* Its key gone from the keychain (a copy from another Mac): the same. */
    clear_keys();
    CHECK(boot(x4) == 0 && memcmp(x3, x4, 32), "a missing key kept the TPM");
    snprintf(bk, sizeof bk, "backup-%s/permall", id3);
    CHECK(exists(bk), "no %s", bk);
    mn_tpm_stop();

    clear_keys();
    char rm[1100];
    snprintf(rm, sizeof rm, "rm -rf '%s'", dir);
    system(rm);
    if (!failed) printf("tpm-engine: ok\n");
    return failed;
}
