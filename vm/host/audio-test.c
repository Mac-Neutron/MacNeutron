/* audio.c against a stand-in guest: playback pacing, STOP/PAUSE/START, 150 ms focus mute fade DSP & live clock,
 * and 200 ms TCC-isolated MNAC microphone capture (none, denied, hang, sine). Prints "audio-test: ok" or first failure.
 *   audio-test             on the Mac's output (silence), once CoreAudio answers
 *   MN_AUDIO_OUTPUT=none   on the stand-in output that plays into nothing at the stream's rate
 *   MN_AUDIO_OUTPUT=slow   an output that takes 3 s to make, as a stuck CoreAudio: the connection must keep reading
 * vm/tests/audio-host.sh builds it and runs all three. */
#include "audio.h"
#include "audio_tcc.h"
#include "../guest/mn-audio.h"
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static const char *sock;

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void fail(const char *what) {
    printf("audio-test: FAIL %s\n", what);
    exit(1);
}

static int read_exact(int fd, void *buf, size_t n) {
    uint8_t *p = buf;
    while (n) {
        ssize_t got = read(fd, p, n);
        if (got <= 0) return -1;
        p += got;
        n -= (size_t)got;
    }
    return 0;
}

static int stream(unsigned channels) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    strcpy(a.sun_path, sock);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) fail("connect");
    struct mn_audio_hello h = {MN_AUDIO_MAGIC, 48000, channels, MN_AUDIO_S16};
    if (write(fd, &h, sizeof h) != sizeof h) fail("hello");
    return fd;
}

static int cap_stream(unsigned rate, unsigned channels, unsigned format, uint32_t buffer_frames) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    strcpy(a.sun_path, sock);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) fail("cap connect");
    struct mn_audio_hello h = {MN_AUDIO_MAGIC_CAP, rate, channels, format};
    if (write(fd, &h, sizeof h) != sizeof h || write(fd, &buffer_frames, sizeof buffer_frames) != sizeof buffer_frames)
        fail("cap hello");
    return fd;
}

static void msg(int fd, uint32_t type, uint32_t frames) {
    static char silence[48000 * 4];
    struct mn_audio_msg m = {type, frames * 4};
    if (write(fd, &m, sizeof m) != sizeof m) fail("message");
    if (frames && write(fd, silence, frames * 4) != (ssize_t)frames * 4) fail("frames");
}

static void cap_cmd(int fd, uint32_t type) {
    struct mn_audio_msg m = {type, 0};
    if (write(fd, &m, sizeof m) != sizeof m) fail("cap cmd");
}

static struct mn_audio_msg cap_wait_status(int fd, int timeout_ms) {
    struct pollfd p = {.fd = fd, .events = POLLIN};
    int rc = poll(&p, 1, timeout_ms);
    if (rc <= 0) fail("cap status poll timeout");
    struct mn_audio_msg m;
    if (read_exact(fd, &m, sizeof m)) fail("cap status read");
    return m;
}

/* The host's count (sent only when it changes), once it reaches want or the deadline passes; -1 if the stream
 * ended. */
static uint64_t last;
static long long played(int fd, uint64_t want, double deadline) {
    struct timeval tv = {0, 50000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    while (last < want && now() < deadline) {
        uint64_t v;
        ssize_t n = read(fd, &v, sizeof v);
        if (n == 0) return -1;
        if (n == sizeof v) last = v;
    }
    return (long long)last;
}

static void test_fade_dsp(void) {
    enum { N = 7200 };
    int16_t *s16 = malloc(N * 2 * sizeof(int16_t));
    float *f32 = malloc(N * 2 * sizeof(float));
    if (!s16 || !f32) fail("fade malloc");

    /* 1. S16 48 kHz 1.0 -> 0.0 ramp in exact 7200 frames */
    for (int i = 0; i < N * 2; i++) s16[i] = 10000;
    float p = mn_audio_apply_fade((uint8_t *)s16, N, 2, 0, 48000, 1.0f, 1);
    if (p != 0.0f) fail("fade s16 down phase");
    for (int i = 1; i < N; i++) {
        if (s16[i * 2] > s16[(i - 1) * 2]) fail("fade s16 down not monotonic");
        if (s16[i * 2] != s16[i * 2 + 1]) fail("fade s16 down channel mismatch");
    }
    if (s16[(N / 2 - 1) * 2] < 4990 || s16[(N / 2 - 1) * 2] > 5010) fail("fade s16 midpoint");
    if (s16[(N - 1) * 2] != 0) fail("fade s16 down end not zero");

    /* Also verify 7199 frames does NOT finish yet, and 1 more frame reaches exact 0.0 */
    for (int i = 0; i < N * 2; i++) s16[i] = 10000;
    p = mn_audio_apply_fade((uint8_t *)s16, N - 1, 2, 0, 48000, 1.0f, 1);
    if (p <= 0.0f) fail("fade s16 finished too early");
    p = mn_audio_apply_fade((uint8_t *)&s16[(N - 1) * 2], 1, 2, 0, 48000, p, 1);
    if (p != 0.0f) fail("fade s16 7200th frame did not finish");

    /* 2. Steady-state muted S16 -> exact 0 */
    for (int i = 0; i < 1024 * 2; i++) s16[i] = 12345;
    p = mn_audio_apply_fade((uint8_t *)s16, 1024, 2, 0, 48000, 0.0f, 1);
    if (p != 0.0f) fail("fade s16 steady mute phase");
    for (int i = 0; i < 1024 * 2; i++) {
        if (s16[i] != 0) fail("fade s16 steady mute non-zero");
    }

    /* 3. S16 48 kHz 0.0 -> 1.0 ramp in exact 7200 frames */
    for (int i = 0; i < N * 2; i++) s16[i] = 10000;
    p = mn_audio_apply_fade((uint8_t *)s16, N, 2, 0, 48000, 0.0f, 0);
    if (p != 1.0f) fail("fade s16 up phase");
    for (int i = 1; i < N; i++) {
        if (s16[i * 2] < s16[(i - 1) * 2]) fail("fade s16 up not monotonic");
    }
    if (s16[0] > 10 || s16[(N - 1) * 2] != 10000) fail("fade s16 up endpoints");

    /* 4. F32 48 kHz chunked ramp 1.0 -> 0.0 and 0.0 -> 1.0 */
    for (int i = 0; i < N * 2; i++) f32[i] = 1.0f;
    p = 1.0f;
    for (int off = 0; off < N; off += 480) {
        p = mn_audio_apply_fade((uint8_t *)&f32[off * 2], 480, 2, 1, 48000, p, 1);
    }
    if (p != 0.0f) fail("fade f32 chunked down phase");
    for (int i = 1; i < N; i++) {
        if (f32[i * 2] > f32[(i - 1) * 2] + 1e-6f) fail("fade f32 down not monotonic");
    }
    if (f32[(N - 1) * 2] != 0.0f) fail("fade f32 down end not zero");

    /* Steady-state unmuted F32 keeps gain 1.0 */
    for (int i = 0; i < 512 * 2; i++) f32[i] = 0.75f;
    p = mn_audio_apply_fade((uint8_t *)f32, 512, 2, 1, 48000, 1.0f, 0);
    if (p != 1.0f || f32[0] != 0.75f || f32[512 * 2 - 1] != 0.75f) fail("fade f32 steady unmute");

    /* 5. Mid-fade reversal at 75 ms (3600 frames) with zero jump/discontinuity */
    for (int i = 0; i < N * 2; i++) f32[i] = 1.0f;
    p = mn_audio_apply_fade((uint8_t *)f32, 3600, 2, 1, 48000, 1.0f, 1);
    if (fabsf(p - 0.5f) > 1e-4f) fail("fade mid-reversal phase");
    float last_down = f32[(3600 - 1) * 2];
    if (fabsf(last_down - 0.5f) > 1e-3f) fail("fade mid-reversal value");
    p = mn_audio_apply_fade((uint8_t *)&f32[3600 * 2], 3600, 2, 1, 48000, p, 0);
    float first_up = f32[3600 * 2];
    if (first_up < last_down || fabsf(first_up - last_down) > 0.002f) fail("fade mid-reversal discontinuity");
    if (p != 1.0f) fail("fade mid-reversal did not return to 1.0");

    free(s16);
    free(f32);
    printf("fade dsp: S16/F32 7200-frame ramp, steady mute, and mid-fade reversal ok\n");
}

static void check_mute_bg(void) {
    /* Must run in a fresh process with MN_AUDIO_MUTE_BACKGROUND=0 before mn_audio_listen(). */
    if (mn_audio_mute_in_background_enabled() != 0) fail("MN_AUDIO_MUTE_BACKGROUND=0 did not disable background mute");
    int16_t s16[512 * 2];
    for (int i = 0; i < 512 * 2; i++) s16[i] = 10000;
    float p = mn_audio_apply_fade((uint8_t *)s16, 512, 2, 0, 48000, 1.0f, mn_audio_mute_in_background_enabled());
    if (p != 1.0f || s16[0] != 10000 || s16[512 * 2 - 1] != 10000) fail("MN_AUDIO_MUTE_BACKGROUND=0 faded samples");

    char cpath[] = "/tmp/mn-audio-bg-XXXXXX";
    if (!mkdtemp(cpath)) fail("mkdtemp mute_bg");
    char csock[128];
    snprintf(csock, sizeof csock, "%s/audio.sock", cpath);
    sock = csock;
    if (mn_audio_listen(csock)) fail("listen mute_bg");
    mn_audio_set_focus_active(0);
    if (mn_audio_mute_in_background_enabled() != 0) fail("mn_audio_listen reset MN_AUDIO_MUTE_BACKGROUND=0");

    int fd = stream(2);
    last = 0;
    msg(fd, MN_AUDIO_DATA, 9600);
    msg(fd, MN_AUDIO_START, 0);
    if (played(fd, 9600, now() + 2.0) != 9600) fail("MN_AUDIO_MUTE_BACKGROUND=0 playback stalled");
    close(fd);
    unlink(csock);
    rmdir(cpath);

    mn_audio_set_mute_in_background(1);
    if (mn_audio_mute_in_background_enabled() != 1) fail("set_mute_in_background(1)");
    mn_audio_set_mute_in_background(0);
    if (mn_audio_mute_in_background_enabled() != 0) fail("set_mute_in_background(0)");
    printf("mute_bg: MN_AUDIO_MUTE_BACKGROUND=0 verified ok\n");
}

static void test_focus_mute_live(void) {
    mn_audio_set_mute_in_background(1);
    mn_audio_set_focus_active(0);
    int fd = stream(2);
    last = 0;
    msg(fd, MN_AUDIO_DATA, 24000);
    double t0 = now();
    msg(fd, MN_AUDIO_START, 0);
    long long got = played(fd, 24000, t0 + 3.0);
    double took = now() - t0;
    mn_audio_set_focus_active(1);
    close(fd);
    printf("focus mute live: %lld frames played while muted in %.3f s\n", got, took);
    if (got != 24000 || took < 0.45 || took > 1.2) fail("focus mute stalled audio clock");
}

static void test_capture(void) {
    char desc[128] = "";
    mn_audio_describe_input_device(0, desc, sizeof desc);
    if (!desc[0]) fail("describe_input_device empty");

    /* Invalid capture hello (6 channels) must be refused cleanly */
    {
        int fd = cap_stream(48000, 6, MN_AUDIO_S16, 1920);
        struct pollfd p = {.fd = fd, .events = POLLIN | POLLHUP};
        if (poll(&p, 1, 1000) <= 0) fail("invalid cap not closed");
        uint8_t b;
        if (read(fd, &b, 1) != 0) fail("invalid cap sent data");
        close(fd);
    }

    /* 1. MN_AUDIO_INPUT=none -> MN_AUDIO_ERR_NODEV < 50 ms */
    setenv("MN_AUDIO_INPUT", "none", 1);
    {
        int fd = cap_stream(48000, 2, MN_AUDIO_S16, 1920);
        double t0 = now();
        cap_cmd(fd, MN_AUDIO_START);
        struct mn_audio_msg st = cap_wait_status(fd, 500);
        double took = now() - t0;
        close(fd);
        printf("capture none: status=%u in %.4f s\n", st.bytes, took);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_ERR_NODEV || took > 0.05) fail("capture none");
    }

    /* 2. MN_AUDIO_INPUT=denied -> MN_AUDIO_ERR_DENIED < 50 ms */
    setenv("MN_AUDIO_INPUT", "denied", 1);
    {
        int fd = cap_stream(48000, 2, MN_AUDIO_S16, 1920);
        double t0 = now();
        cap_cmd(fd, MN_AUDIO_START);
        struct mn_audio_msg st = cap_wait_status(fd, 500);
        double took = now() - t0;
        close(fd);
        printf("capture denied: status=%u in %.4f s\n", st.bytes, took);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_ERR_DENIED || took > 0.05) fail("capture denied");
    }

    /* 3. MN_AUDIO_INPUT=hang -> MN_AUDIO_ERR_TIMEOUT in 180..350 ms + concurrent playback unaffected */
    setenv("MN_AUDIO_INPUT", "hang", 1);
    {
        int pfd = stream(2);
        last = 0;
        msg(pfd, MN_AUDIO_DATA, 48000);
        double pt0 = now();
        msg(pfd, MN_AUDIO_START, 0);

        int cfd = cap_stream(48000, 2, MN_AUDIO_S16, 1920);
        double t0 = now();
        cap_cmd(cfd, MN_AUDIO_START);
        struct mn_audio_msg st = cap_wait_status(cfd, 1000);
        double took = now() - t0;
        close(cfd); /* late-start 5 s worker will see cancelled/closing and clean up */
        printf("capture hang: status=%u in %.3f s\n", st.bytes, took);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_ERR_TIMEOUT || took < 0.18 || took > 0.35)
            fail("capture hang");

        long long pgot = played(pfd, 48000, pt0 + 2.5);
        double ptook = now() - pt0;
        close(pfd);
        printf("capture hang concurrent playback: %lld frames in %.3f s\n", pgot, ptook);
        if (pgot != 48000 || ptook < 0.85 || ptook > 1.6) fail("capture hang affected playback");
    }

    /* 4. MN_AUDIO_INPUT=sine -> 1-ch 16 kHz S16 with START / PAUSE / START / STOP */
    setenv("MN_AUDIO_INPUT", "sine", 1);
    {
        int fd = cap_stream(16000, 1, MN_AUDIO_S16, 1600);
        cap_cmd(fd, MN_AUDIO_START);
        struct mn_audio_msg st = cap_wait_status(fd, 500);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_OK) fail("capture sine mono start");

        size_t total_bytes = 0;
        int nonzero = 0;
        double t0 = now();
        while (total_bytes < 4000 * 2 && now() - t0 < 1.5) {
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, 200) <= 0) continue;
            struct mn_audio_msg m;
            if (read_exact(fd, &m, sizeof m)) fail("capture sine mono hdr");
            if (m.type != MN_AUDIO_DATA || m.bytes == 0 || m.bytes > 65536) fail("capture sine mono msg");
            int16_t buf[32768];
            if (read_exact(fd, buf, m.bytes)) fail("capture sine mono body");
            for (size_t i = 0; i < m.bytes / 2; i++) {
                if (buf[i] != 0) nonzero = 1;
            }
            total_bytes += m.bytes;
        }
        if (total_bytes < 4000 * 2 || !nonzero) fail("capture sine mono frames");

        /* PAUSE stops streaming, then START resumes */
        cap_cmd(fd, MN_AUDIO_PAUSE);
        usleep(40000);
        for (;;) {
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, 30) <= 0) break;
            struct mn_audio_msg m;
            if (read_exact(fd, &m, sizeof m)) fail("capture sine pause drain hdr");
            if (m.type != MN_AUDIO_DATA || m.bytes > 65536) fail("capture sine pause drain type");
            uint8_t discard[65536];
            if (m.bytes && read_exact(fd, discard, m.bytes)) fail("capture sine pause drain body");
        }
        {
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, 80) > 0) fail("capture sine streamed while paused");
        }

        cap_cmd(fd, MN_AUDIO_START);
        st = cap_wait_status(fd, 500);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_OK) fail("capture sine mono resume");
        size_t resumed_bytes = 0;
        t0 = now();
        while (resumed_bytes < 1600 * 2 && now() - t0 < 1.0) {
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, 200) <= 0) continue;
            struct mn_audio_msg m;
            if (read_exact(fd, &m, sizeof m)) fail("capture sine resume hdr");
            if (m.type != MN_AUDIO_DATA || m.bytes > 65536) fail("capture sine resume msg");
            uint8_t buf[65536];
            if (m.bytes && read_exact(fd, buf, m.bytes)) fail("capture sine resume body");
            resumed_bytes += m.bytes;
        }
        if (resumed_bytes < 1600 * 2) fail("capture sine mono resume frames");
        cap_cmd(fd, MN_AUDIO_STOP);
        close(fd);
        printf("capture sine mono 16k S16: %zu + %zu bytes ok\n", total_bytes, resumed_bytes);
    }

    /* 5. MN_AUDIO_INPUT=sine -> 2-ch 48 kHz F32 */
    {
        int fd = cap_stream(48000, 2, MN_AUDIO_F32, 1920);
        cap_cmd(fd, MN_AUDIO_START);
        struct mn_audio_msg st = cap_wait_status(fd, 500);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_OK) fail("capture sine stereo start");
        size_t total_bytes = 0;
        int nonzero = 0;
        double t0 = now();
        while (total_bytes < 4800 * 8 && now() - t0 < 1.5) {
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, 200) <= 0) continue;
            struct mn_audio_msg m;
            if (read_exact(fd, &m, sizeof m)) fail("capture sine stereo hdr");
            if (m.type != MN_AUDIO_DATA || m.bytes == 0 || m.bytes > 65536) fail("capture sine stereo msg");
            float buf[16384];
            if (read_exact(fd, buf, m.bytes)) fail("capture sine stereo body");
            for (size_t i = 0; i < m.bytes / 8; i++) {
                float l = buf[i * 2], r = buf[i * 2 + 1];
                if (!isfinite(l) || fabsf(l) > 1.0f || l != r) fail("capture sine stereo sample");
                if (l != 0.0f) nonzero = 1;
            }
            total_bytes += m.bytes;
        }
        if (total_bytes < 4800 * 8 || !nonzero) fail("capture sine stereo frames");
        cap_cmd(fd, MN_AUDIO_STOP);
        close(fd);
        printf("capture sine stereo 48k F32: %zu bytes ok\n", total_bytes);
    }

    /* 6. Huge buffer_frames clamped + stalled guest STOP/START nonblocking TX */
    {
        int fd = cap_stream(48000, 2, MN_AUDIO_F32, 0xffffffffu);
        int rcvbuf = 4096;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof rcvbuf);
        cap_cmd(fd, MN_AUDIO_START);
        struct mn_audio_msg st = cap_wait_status(fd, 500);
        if (st.type != MN_AUDIO_STATUS || st.bytes != MN_AUDIO_OK) fail("capture backpressure start");
        usleep(120000); /* stall reads so host hits EAGAIN while filling socket */
        cap_cmd(fd, MN_AUDIO_STOP);
        cap_cmd(fd, MN_AUDIO_START);
        int got_ok = 0;
        double dl = now() + 1.0;
        while (!got_ok && now() < dl) {
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, 200) <= 0) continue;
            struct mn_audio_msg m;
            if (read_exact(fd, &m, sizeof m)) fail("capture backpressure hdr");
            if (m.type == MN_AUDIO_STATUS) {
                if (m.bytes != MN_AUDIO_OK) fail("capture backpressure restart status");
                got_ok = 1;
            } else if (m.type == MN_AUDIO_DATA) {
                if (m.bytes > 65536) fail("capture backpressure data size");
                uint8_t discard[65536];
                if (m.bytes && read_exact(fd, discard, m.bytes)) fail("capture backpressure body");
            } else {
                fail("capture backpressure msg type");
            }
        }
        if (!got_ok) fail("capture backpressure restart timed out");
        cap_cmd(fd, MN_AUDIO_STOP);
        close(fd);
        printf("capture backpressure + clamped buffer_frames ok\n");
    }

    unsetenv("MN_AUDIO_INPUT");
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN); /* a stream the host ends fails its write, and the test says so */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "--check-mute-bg")) {
        check_mute_bg();
        return 0;
    }
    char path[] = "/tmp/mn-audio-test-XXXXXX";
    if (!mkdtemp(path)) fail("mkdtemp");
    static char p[128];
    snprintf(p, sizeof p, "%s/audio.sock", path);
    sock = p;
    if (mn_audio_listen(sock)) fail("listen");
    const char *mode = getenv("MN_AUDIO_OUTPUT");
    if (mode && !strcmp(mode, "slow")) {
        /* An output that takes 3 s to make (CoreAudio stuck) must not stop the connection reading: libkrun's vsock
         * proxy sends to it with blocking calls, from the thread every vsock connection and the console share, and an
         * unread UNIX socket here holds about 8 KiB. */
        int fd = stream(2);
        double t0 = now();
        for (int i = 0; i < 16; i++) msg(fd, MN_AUDIO_DATA, 4096); /* 256 KiB */
        double took = now() - t0;
        printf("slow output: 256 KiB taken in %.3f s\n", took);
        if (took > 1) fail("slow output: the connection stopped reading");
        msg(fd, MN_AUDIO_START, 0);
        if (played(fd, 65536, now() + 5) != 65536) fail("slow output: never played");
        printf("audio-test: ok\n");
        return 0;
    }
    usleep(200000);  /* CoreAudio's first answer, when it answers */

    /* Half a second, sent at once: played as the output's clock goes, not as fast as it came. */
    int fd = stream(2);
    msg(fd, MN_AUDIO_DATA, 24000);
    double t0 = now();
    msg(fd, MN_AUDIO_START, 0);
    long long got = played(fd, 24000, t0 + 3);
    double took = now() - t0;
    printf("paced: %lld frames in %.3f s\n", got, took);
    if (got != 24000) fail("paced: not all played");
    if (took < 0.45 || took > 1.2) fail("paced: not in real time");

    /* A second queued, a tenth of it played, then STOP: the rest dropped, and counted, at once. */
    msg(fd, MN_AUDIO_DATA, 48000);
    msg(fd, MN_AUDIO_START, 0);
    usleep(100000);
    t0 = now();
    msg(fd, MN_AUDIO_STOP, 0);
    got = played(fd, 72000, t0 + 1);
    took = now() - t0;
    printf("stop: %lld frames in %.3f s\n", got, took);
    if (got != 72000 || took > 0.3) fail("stop");

    /* PAUSE keeps what's queued: nothing more is played until START. */
    msg(fd, MN_AUDIO_DATA, 4800);
    msg(fd, MN_AUDIO_PAUSE, 0);
    got = played(fd, 72001, now() + 0.3);
    printf("paused: %lld frames\n", got);
    if (got != 72000) fail("pause");
    msg(fd, MN_AUDIO_START, 0);
    if (played(fd, 76800, now() + 1) != 76800) fail("resume");
    close(fd);

    /* A game writing ahead (the host's ring full, as in a match): beyond a wake-up for each message it reads, the
     * connection wakes about once a report interval, not every millisecond, and its position still goes out. Its last
     * line, at the close, has the rates. */
    char log[] = "/tmp/mn-audio-calm-XXXXXX";
    close(mkstemp(log));
    fflush(stderr);
    int saved = dup(2), lf = open(log, O_WRONLY | O_TRUNC);
    dup2(lf, 2);
    close(lf);
    fd = stream(2), last = 0;
    msg(fd, MN_AUDIO_START, 0);
    t0 = now();
    while (now() - t0 < 2.5) { /* 10 ms chunks, whole, as fast as the host takes them (it blocks the writes) */
        msg(fd, MN_AUDIO_DATA, 480);
        uint64_t v;
        while (recv(fd, &v, sizeof v, MSG_DONTWAIT) == sizeof v) last = v;
    }
    long long moved = (long long)last;
    close(fd);
    usleep(200000); /* the connection's thread prints its last line */
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    char text[2048] = "";
    FILE *f = fopen(log, "r");
    size_t n = f ? fread(text, 1, sizeof text - 1, f) : 0;
    text[n] = 0;
    if (f) fclose(f);
    unlink(log);
    const char *line = strstr(text, " turns, ");
    double turns = 0, messages = 0;
    if (line) {
        const char *t = line;
        while (t > text && t[-1] != ' ') t--;
        turns = atof(t);
        messages = atof(line + strlen(" turns, "));
    }
    printf("calm: %.0f turns a second, %.0f of them for messages; position at %lld frames\n", turns, messages, moved);
    if (!line || turns - messages > 150) fail("calm: the connection wakes too often for nothing");
    if (moved < 48000) fail("calm: the position stopped moving");

    /* A small buffer (Wine's shortest: four 5 ms periods), said in an MNA2 hello: the position goes out every quarter
     * of it, so the guest never waits a report interval longer than its buffer can cover. The stand-in output takes
     * frames every 5-6 ms, and a report goes out when they moved: every 5 ms the guest hears each step (about 180 a
     * second), every 10 ms about 100. CoreAudio takes them a cycle (about 10.7 ms) at a time: there it only prints. */
    {
        struct sockaddr_un a = {.sun_family = AF_UNIX};
        strcpy(a.sun_path, sock);
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) fail("connect");
        struct mn_audio_hello h = {MN_AUDIO_MAGIC2, 48000, 2, MN_AUDIO_S16};
        uint32_t buffer = 960; /* 20 ms */
        if (write(fd, &h, sizeof h) != sizeof h || write(fd, &buffer, sizeof buffer) != sizeof buffer) fail("hello");
    }
    last = 0;
    msg(fd, MN_AUDIO_DATA, 48000);
    msg(fd, MN_AUDIO_START, 0);
    if (played(fd, 4800, now() + 1) < 4800) fail("small buffer: never played");
    int reports = 0;
    for (t0 = now(); now() - t0 < 0.5; reports++)
        if (played(fd, last + 1, now() + 0.1) < 0) fail("small buffer: the stream ended");
    close(fd);
    printf("small buffer: %d reports a second\n", reports * 2);
    if (mode && !strcmp(mode, "none") && reports * 2 < 150) fail("small buffer: the position comes too seldom");

    /* Six channels: the guest's plugin never asks for them (games downmix to its two). */
    fd = stream(6), last = 0;
    if (played(fd, 1, now() + 1) != -1) fail("six channels not refused");
    close(fd);

    test_fade_dsp();
    test_focus_mute_live();
    test_capture();

    printf("audio-test: ok\n");
    return 0;
}
