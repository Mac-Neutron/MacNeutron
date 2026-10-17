/* The guest's TPM 2.0 (macneutron-vm --tpm <dir>; spec §6 of the research README, EAC v2): libkrun's TpmDevice, a
 * TIS interface that Linux's built-in tpm_tis binds to, with this file as its engine.
 * ponytail: no engine yet. Every command answers TPM_RC_FAILURE and is logged ("mn-tpm: command 0x..."), which proves
 * the device, its device-tree node and the TIS exchange against the guest's driver (eac-vm/tests/tpm-host.sh): Linux
 * sends a command only once it has read the last one's response. It registers no chip that way (its boot-time
 * sessions need a primary key), so /dev/tpm0 waits for the engine: libtpms (TPMLIB_Process, its NV state in dir). */
#include "tpm.h"
#include <stdio.h>

static int32_t execute(void *userdata, uint8_t locality, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap) {
    (void)userdata, (void)resp, (void)cap;
    unsigned code = len >= 10 ? (unsigned)cmd[6] << 24 | cmd[7] << 16 | cmd[8] << 8 | cmd[9] : 0;
    fprintf(stderr, "mn-tpm: command 0x%x (%zu bytes, locality %u): no engine\n", code, len, locality);
    return -1;
}

KrunTpmDevice mn_tpm_device(const char *dir, KrunError *err) {
    static const struct krun_tpm_backend backend = {NULL, execute};
    (void)dir;
    return krun_tpm_device_new(&backend, sizeof backend, err);
}
