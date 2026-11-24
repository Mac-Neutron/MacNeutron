/* The guest's sound (audio.c). */
#ifndef MN_HOST_AUDIO_H
#define MN_HOST_AUDIO_H

#include <stdint.h>

/* Serves the guest's audio streams on a UNIX socket at path (what the vsock device maps MN_AUDIO_PORT to), each
 * through its own CoreAudio output (playback) or TCC-isolated CoreAudio input (capture). 0, or -1 with errno set. */
int mn_audio_listen(const char *path);

/* Controls whether the VM window is currently active/visible. When inactive and background muting is enabled,
 * active playback streams ramp smoothly to silence over 150 ms while keeping the guest audio clock advancing. */
void mn_audio_set_focus_active(int active);

/* Overrides MN_AUDIO_MUTE_BACKGROUND (default enabled unless env var is "0"). */
void mn_audio_set_mute_in_background(int enabled);

/* Returns whether background muting is currently enabled (initializes from MN_AUDIO_MUTE_BACKGROUND once). */
int mn_audio_mute_in_background_enabled(void);

/* Applies a 150 ms C1 smoothstep focus mute/unmute ramp across frames interleaved samples in buf, advancing
 * and returning the new fade phase in [0.0f, 1.0f]. */
float mn_audio_apply_fade(uint8_t *buf, uint64_t frames, unsigned channels, int f32, unsigned rate, float phase,
                          int want_mute);

#endif
