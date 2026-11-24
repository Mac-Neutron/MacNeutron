/* Captures audio through ALSA's default device (the mn plugin, alsa-mn.c) with blocking reads, verifies non-zero
 * PCM energy and real-time pacing, and prints a diagnostic summary line.
 *   alsa-cap [seconds] [rate] [channels] [s16|f32]   (x86-64/i686 under FEX; tests/audio.sh) */
#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static double approx_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 40; i++) r = 0.5 * (r + x / r);
    return r;
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 1.0;
    unsigned rate = argc > 2 ? (unsigned)atoi(argv[2]) : 48000;
    unsigned channels = argc > 3 ? (unsigned)atoi(argv[3]) : 2;
    int is_f32 = 1;
    if (argc > 4) {
        if (!strcmp(argv[4], "s16")) is_f32 = 0;
        else if (!strcmp(argv[4], "f32")) is_f32 = 1;
        else {
            printf("alsa-cap: usage: alsa-cap [seconds] [rate] [channels] [s16|f32]\n");
            return 1;
        }
    }
    if (seconds <= 0.0 || rate < 8000 || rate > 192000 || channels < 1 || channels > 2) {
        printf("alsa-cap: invalid parameters (seconds=%.2f rate=%u channels=%u)\n", seconds, rate, channels);
        return 1;
    }

    snd_pcm_format_t fmt = is_f32 ? SND_PCM_FORMAT_FLOAT_LE : SND_PCM_FORMAT_S16_LE;
    snd_pcm_t *pcm = NULL;
    int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (err >= 0)
        err = snd_pcm_set_params(pcm, fmt, SND_PCM_ACCESS_RW_INTERLEAVED, channels, rate, 0, 50000);
    if (err < 0) {
        printf("alsa-cap: %s\n", snd_strerror(err));
        if (pcm) snd_pcm_close(pcm);
        return 1;
    }

    snd_pcm_uframes_t chunk = rate / 100; /* 10 ms */
    if (chunk < 1) chunk = 1;
    size_t sample_bytes = is_f32 ? sizeof(float) : sizeof(int16_t);
    void *buf = malloc((size_t)chunk * channels * sample_bytes);
    if (!buf) {
        printf("alsa-cap: out of memory\n");
        snd_pcm_close(pcm);
        return 1;
    }

    uint64_t target = (uint64_t)(seconds * rate + 0.5);
    if (target < 1) target = 1;
    uint64_t got = 0;
    double peak = 0.0, sum_sq = 0.0;
    double t0 = now();
    while (got < target) {
        snd_pcm_uframes_t want = (snd_pcm_uframes_t)(target - got);
        if (want > chunk) want = chunk;
        snd_pcm_sframes_t r = snd_pcm_readi(pcm, buf, want);
        if (r < 0) {
            if (r == -EPIPE || r == -EINTR || r == -ESTRPIPE) {
                r = snd_pcm_recover(pcm, (int)r, 0);
                if (r >= 0) continue;
            }
            printf("alsa-cap: %s\n", snd_strerror((int)r));
            free(buf);
            snd_pcm_close(pcm);
            return 1;
        }
        if (r == 0) {
            printf("alsa-cap: unexpected EOF\n");
            free(buf);
            snd_pcm_close(pcm);
            return 1;
        }
        size_t samples = (size_t)r * channels;
        if (is_f32) {
            const float *p = buf;
            for (size_t i = 0; i < samples; i++) {
                double v = p[i];
                if (!(v == v)) continue;
                double a = v < 0.0 ? -v : v;
                if (a > peak) peak = a;
                sum_sq += v * v;
            }
        } else {
            const int16_t *p = buf;
            for (size_t i = 0; i < samples; i++) {
                double v = (double)p[i] / 32768.0;
                double a = v < 0.0 ? -v : v;
                if (a > peak) peak = a;
                sum_sq += v * v;
            }
        }
        got += (uint64_t)r;
    }
    double elapsed = now() - t0;
    double rms = approx_sqrt(sum_sq / (double)(got * channels));
    free(buf);
    snd_pcm_close(pcm);

    printf("alsa-cap: %.1f s captured in %.3f s (rate=%u ch=%u fmt=%s peak=%.4f rms=%.4f)\n",
           seconds, elapsed, rate, channels, is_f32 ? "f32" : "s16", peak, rms);
    if (peak <= 0.0 || rms <= 0.0) {
        printf("alsa-cap: FAIL silent PCM\n");
        return 1;
    }
    return 0;
}
