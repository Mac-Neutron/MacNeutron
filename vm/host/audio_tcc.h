/* Nonblocking macOS TCC microphone authorization and CoreAudio input device description helpers (audio_tcc.m). */
#ifndef MN_HOST_AUDIO_TCC_H
#define MN_HOST_AUDIO_TCC_H

#include <stddef.h>
#include <stdint.h>

enum {
    MN_MIC_AUTHORIZED = 0,
    MN_MIC_NOT_DETERMINED = 1,
    MN_MIC_DENIED = 2,
};

/* Nonblocking check of macOS microphone TCC authorization. When NotDetermined, asynchronously requests access
 * at most once per process without blocking and returns MN_MIC_NOT_DETERMINED. */
int mn_audio_mic_auth_status(void);

/* Best-effort CoreAudio input device name/UID description for logging. Always NUL-terminates buf if len > 0. */
void mn_audio_describe_input_device(uint32_t device_id, char *buf, size_t len);

#endif
