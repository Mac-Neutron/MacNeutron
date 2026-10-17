/* The guest's TPM 2.0 (tpm.c). */
#ifndef MN_HOST_TPM_H
#define MN_HOST_TPM_H
#include "libkrun.h"
#include "libkrun_tpm.h"

/* libkrun's TpmDevice (eac-vm/libkrun patch 0007) with this host's engine; its state in dir. NULL with *err set if
 * libkrun refuses it. */
KrunTpmDevice mn_tpm_device(const char *dir, KrunError *err);

#endif
