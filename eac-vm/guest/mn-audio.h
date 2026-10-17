/* The game's sound between the guest's ALSA plugin (alsa-mn.c) and the VM host's CoreAudio player
 * (eac-vm/host/audio.c): one ALSA stream a connection, on vsock port MN_AUDIO_PORT (the host's UNIX socket).
 * Little-endian on both sides.
 *   guest -> host  a hello, then messages: a header, and for DATA that many bytes of interleaved frames
 *   host -> guest  the frames played (or dropped) so far, a uint64_t, whenever that changes: the guest's hardware
 *                  pointer, so the game is paced by the Mac's audio clock */
#ifndef MN_AUDIO_H
#define MN_AUDIO_H
#include <stdint.h>

#define MN_AUDIO_PORT 5102
#define MN_AUDIO_MAGIC 0x31414e4du /* "MNA1" */

enum { MN_AUDIO_S16 = 0, MN_AUDIO_F32 = 1 };
struct mn_audio_hello {
    uint32_t magic, rate, channels, format;
};

enum {
    MN_AUDIO_DATA = 1,  /* frames to queue */
    MN_AUDIO_START = 2, /* play what's queued and what comes */
    MN_AUDIO_STOP = 3,  /* drop what's queued and stop; the answer counts the dropped frames as played */
    MN_AUDIO_PAUSE = 4, /* stop, keeping what's queued */
};
struct mn_audio_msg {
    uint32_t type, bytes;
};

#endif
