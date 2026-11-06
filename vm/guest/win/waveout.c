/* Plays two seconds of silence through winmm's waveOut (in Wine: mmdevapi, winealsa, the guest's mn plugin), as a
 * game's sound goes, and prints how long it took: paced by the host, it takes its own length.
 *   waveout.exe   (tests/audio.sh) */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

int main(void) {
    static char silence[48000 * 4 * 2];
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, 2, 48000, 48000 * 4, 4, 16, 0};
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    HWAVEOUT out;
    MMRESULT r = waveOutOpen(&out, WAVE_MAPPER, &format, (DWORD_PTR)done, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        printf("waveout: open failed (%u)\n", r);
        return 1;
    }
    WAVEHDR h = {.lpData = silence, .dwBufferLength = sizeof silence};
    waveOutPrepareHeader(out, &h, sizeof h);
    LARGE_INTEGER hz, t0, t1;
    QueryPerformanceFrequency(&hz);
    QueryPerformanceCounter(&t0);
    waveOutWrite(out, &h, sizeof h);
    while (!(h.dwFlags & WHDR_DONE))
        if (WaitForSingleObject(done, 10000) == WAIT_TIMEOUT) {
            printf("waveout: stalled\n");
            return 1;
        }
    QueryPerformanceCounter(&t1);
    printf("waveout: 2.0 s played in %.3f s\n", (double)(t1.QuadPart - t0.QuadPart) / hz.QuadPart);
    waveOutUnprepareHeader(out, &h, sizeof h);
    waveOutClose(out);
    return 0;
}
