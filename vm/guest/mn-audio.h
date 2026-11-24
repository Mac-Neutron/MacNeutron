/* The game's sound between the guest's ALSA plugin (alsa-mn.c) and the VM host's CoreAudio player
 * (vm/host/audio.c): one ALSA stream a connection, on vsock port MN_AUDIO_PORT (the host's UNIX socket).
 * Little-endian on both sides.
 *   guest -> host  a hello, then messages: a header, and for DATA that many bytes of interleaved frames
 *   host -> guest  the frames played (or dropped) so far, a uint64_t, whenever that changes: the guest's hardware
 *                  pointer, so the game is paced by the Mac's audio clock */
#ifndef MN_AUDIO_H
#define MN_AUDIO_H
#include <stdint.h>

#define MN_AUDIO_PORT 5102
#define MN_AUDIO_MAGIC 0x31414e4du /* "MNA1" */
/* "MNA2": the hello is followed by a uint32_t, the guest's ALSA buffer in frames; the host sends the position every
 * quarter of it, 2 to 10 ms (an MNA1 stream: every 10 ms). */
#define MN_AUDIO_MAGIC2 0x32414e4du
/* "MNAC": capture stream hello (followed by uint32_t buffer_frames, matching MNA2's 20-byte header).
 *   guest -> host  mn_audio_hello{MNAC, rate, channels(1..2), format} + uint32_t buffer_frames,
 *                  then mn_audio_msg control headers (START / STOP / PAUSE).
 *   host -> guest  mn_audio_msg{MN_AUDIO_STATUS, status_code} replying to START (bounded to 200 ms),
 *                  then mn_audio_msg{MN_AUDIO_DATA, bytes} + <bytes> of interleaved PCM frames. */
#define MN_AUDIO_MAGIC_CAP 0x43414e4du /* "MNAC" */

enum { MN_AUDIO_S16 = 0, MN_AUDIO_F32 = 1 };
struct mn_audio_hello {
    uint32_t magic, rate, channels, format;
};

enum {
    MN_AUDIO_DATA = 1,   /* frames to queue (playback) or captured frames (capture) */
    MN_AUDIO_START = 2,  /* play what's queued and what comes, or start capture */
    MN_AUDIO_STOP = 3,   /* drop what's queued and stop; the answer counts the dropped frames as played */
    MN_AUDIO_PAUSE = 4,  /* stop, keeping what's queued */
    MN_AUDIO_STATUS = 5, /* host -> guest capture status; bytes = status code below */
};
struct mn_audio_msg {
    uint32_t type, bytes;
};

enum {
    MN_AUDIO_OK = 0,
    MN_AUDIO_ERR_DENIED = 1,  /* macOS TCC microphone permission denied/restricted */
    MN_AUDIO_ERR_NODEV = 2,   /* no default input device */
    MN_AUDIO_ERR_TIMEOUT = 3, /* CoreAudio HAL/TCC exceeded 200 ms start deadline */
    MN_AUDIO_ERR_IO = 4,      /* CoreAudio unit/converter error */
};

#endif
