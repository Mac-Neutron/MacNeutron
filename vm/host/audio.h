/* The guest's sound (audio.c). */
#ifndef MN_HOST_AUDIO_H
#define MN_HOST_AUDIO_H

/* Serves the guest's audio streams on a UNIX socket at path (what the vsock device maps MN_AUDIO_PORT to), each
 * through its own CoreAudio output. 0, or -1 with errno set. */
int mn_audio_listen(const char *path);

#endif
