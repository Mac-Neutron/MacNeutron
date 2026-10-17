/* The guest's TPM 2.0 (tpm.c). */
#ifndef MN_HOST_TPM_H
#define MN_HOST_TPM_H
#include "libkrun.h"
#include "libkrun_tpm.h"

/* The keychain service the TPMs' keys are kept under (a test sets its own). */
extern const char *mn_tpm_service;

/* The TPM's state in dir (made if new) and libtpms on it: 0, or -1 with the reason logged. One TPM per process. */
int mn_tpm_start(const char *dir);
void mn_tpm_stop(void);

/* One TPM 2.0 command into its response (krun_tpm_backend's execute). */
int32_t mn_tpm_execute(void *userdata, uint8_t locality, const uint8_t *cmd, size_t len, uint8_t *resp, size_t cap);

/* libkrun's TpmDevice (eac-vm/libkrun patch 0007) on the started TPM. NULL with *err set if libkrun refuses it. */
KrunTpmDevice mn_tpm_device(KrunError *err);

#endif
