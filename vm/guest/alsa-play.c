/* Plays silence through ALSA's default device (the mn plugin, alsa-mn.c) with blocking writes, as a Linux game's sound
 * goes, and prints how long it took: a stream paced by the host takes its own length.
 *   alsa-play [seconds]   (x86-64, under FEX; tests/audio.sh) */
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 2;
    static float silence[480 * 2]; /* 10 ms at 48 kHz */
    snd_pcm_t *pcm;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err >= 0) err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 0,
                                           50000);
    if (err < 0) {
        printf("alsa-play: %s\n", snd_strerror(err));
        return 1;
    }
    double t0 = now();
    for (int n = (int)(seconds * 100); n > 0; n--) {
        snd_pcm_sframes_t w = snd_pcm_writei(pcm, silence, 480);
        if (w < 0 && (w = snd_pcm_recover(pcm, (int)w, 0)) < 0) {
            printf("alsa-play: %s\n", snd_strerror((int)w));
            return 1;
        }
    }
    snd_pcm_drain(pcm);
    printf("alsa-play: %.1f s played in %.3f s\n", seconds, now() - t0);
    snd_pcm_close(pcm);
    return 0;
}
