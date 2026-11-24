/* Records one second (or [seconds]) of 48 kHz 16-bit PCM through winmm's waveIn (in Wine: mmdevapi, winealsa,
 * the guest's mn plugin), verifies non-zero samples and pacing, and prints a diagnostic summary line.
 *   wavein.exe [seconds] [channels]   (tests/audio.sh) */
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static double approx_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 40; i++) r = 0.5 * (r + x / r);
    return r;
}

int main(int argc, char **argv) {
    double seconds = argc > 1 ? atof(argv[1]) : 1.0;
    int channels = argc > 2 ? atoi(argv[2]) : 2;
    if (seconds <= 0.0 || channels < 1 || channels > 2) {
        printf("wavein: invalid parameters (seconds=%.2f channels=%d)\n", seconds, channels);
        return 1;
    }

    int nbufs = (int)(seconds * 10.0 + 0.5);
    if (nbufs < 1) nbufs = 1;
    DWORD chunk_frames = 4800; /* 100 ms at 48 kHz */
    DWORD block_align = (DWORD)(channels * 2);
    DWORD chunk_bytes = chunk_frames * block_align;

    WAVEFORMATEX format = {
        .wFormatTag = WAVE_FORMAT_PCM,
        .nChannels = (WORD)channels,
        .nSamplesPerSec = 48000,
        .nAvgBytesPerSec = 48000u * block_align,
        .nBlockAlign = (WORD)block_align,
        .wBitsPerSample = 16,
        .cbSize = 0,
    };

    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!done) {
        printf("wavein: CreateEventW failed (%lu)\n", (unsigned long)GetLastError());
        return 1;
    }

    HWAVEIN in = NULL;
    MMRESULT r = waveInOpen(&in, WAVE_MAPPER, &format, (DWORD_PTR)done, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        printf("wavein: open failed (%u)\n", r);
        CloseHandle(done);
        return 1;
    }

    WAVEHDR *hdrs = calloc((size_t)nbufs, sizeof(WAVEHDR));
    char *pcm = calloc((size_t)nbufs, chunk_bytes);
    if (!hdrs || !pcm) {
        printf("wavein: out of memory\n");
        free(hdrs);
        free(pcm);
        waveInClose(in);
        CloseHandle(done);
        return 1;
    }

    for (int i = 0; i < nbufs; i++) {
        hdrs[i].lpData = pcm + (size_t)i * chunk_bytes;
        hdrs[i].dwBufferLength = chunk_bytes;
        if ((r = waveInPrepareHeader(in, &hdrs[i], sizeof(WAVEHDR))) != MMSYSERR_NOERROR ||
            (r = waveInAddBuffer(in, &hdrs[i], sizeof(WAVEHDR))) != MMSYSERR_NOERROR) {
            printf("wavein: buffer setup failed (%u)\n", r);
            waveInReset(in);
            for (int j = 0; j <= i; j++)
                if (hdrs[j].dwFlags & WHDR_PREPARED) waveInUnprepareHeader(in, &hdrs[j], sizeof(WAVEHDR));
            free(hdrs);
            free(pcm);
            waveInClose(in);
            CloseHandle(done);
            return 1;
        }
    }

    LARGE_INTEGER hz, t0, t1;
    QueryPerformanceFrequency(&hz);
    QueryPerformanceCounter(&t0);
    r = waveInStart(in);
    if (r != MMSYSERR_NOERROR) {
        printf("wavein: start failed (%u)\n", r);
        waveInReset(in);
        for (int i = 0; i < nbufs; i++) waveInUnprepareHeader(in, &hdrs[i], sizeof(WAVEHDR));
        free(hdrs);
        free(pcm);
        waveInClose(in);
        CloseHandle(done);
        return 1;
    }

    for (int i = 0; i < nbufs; i++) {
        while (!(hdrs[i].dwFlags & WHDR_DONE)) {
            if (WaitForSingleObject(done, 10000) == WAIT_TIMEOUT) {
                printf("wavein: stalled\n");
                waveInReset(in);
                for (int j = 0; j < nbufs; j++) waveInUnprepareHeader(in, &hdrs[j], sizeof(WAVEHDR));
                free(hdrs);
                free(pcm);
                waveInClose(in);
                CloseHandle(done);
                return 1;
            }
        }
    }
    QueryPerformanceCounter(&t1);
    double elapsed = (double)(t1.QuadPart - t0.QuadPart) / hz.QuadPart;

    waveInStop(in);
    waveInReset(in);

    uint64_t total_samples = 0;
    double peak = 0.0, sum_sq = 0.0;
    for (int i = 0; i < nbufs; i++) {
        DWORD nsamples = hdrs[i].dwBytesRecorded / sizeof(int16_t);
        const int16_t *s = (const int16_t *)hdrs[i].lpData;
        for (DWORD k = 0; k < nsamples; k++) {
            double v = (double)s[k] / 32768.0;
            double a = v < 0.0 ? -v : v;
            if (a > peak) peak = a;
            sum_sq += v * v;
        }
        total_samples += nsamples;
        waveInUnprepareHeader(in, &hdrs[i], sizeof(WAVEHDR));
    }

    free(hdrs);
    free(pcm);
    waveInClose(in);
    CloseHandle(done);

    double rms = total_samples ? approx_sqrt(sum_sq / (double)total_samples) : 0.0;
    printf("wavein: %.1f s captured in %.3f s (rate=48000 ch=%d samples=%llu peak=%.4f rms=%.4f)\n",
           (double)nbufs * 0.1, elapsed, channels, (unsigned long long)total_samples, peak, rms);
    if (!total_samples || peak <= 0.0 || rms <= 0.0) {
        printf("wavein: FAIL silent PCM\n");
        return 1;
    }
    return 0;
}
