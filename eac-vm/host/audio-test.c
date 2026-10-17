/* audio.c against a stand-in guest: a stream is paced by the output's clock, a STOP drops what's queued and counts it
 * as played, PAUSE keeps it, and a stream it can't play is refused. Prints "audio-test: ok" or the first failure.
 *   audio-test             on the Mac's output (silence), once CoreAudio answers
 *   MN_AUDIO_OUTPUT=none   on the stand-in output that plays into nothing at the stream's rate
 *   MN_AUDIO_OUTPUT=slow   an output that takes 3 s to make, as a stuck CoreAudio: the connection must keep reading
 * eac-vm/tests/audio-host.sh builds it and runs both. */
#include "audio.h"
#include "../guest/mn-audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <signal.h>
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

static int stream(unsigned channels) {
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    strcpy(a.sun_path, sock);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a)) fail("connect");
    struct mn_audio_hello h = {MN_AUDIO_MAGIC, 48000, channels, MN_AUDIO_S16};
    if (write(fd, &h, sizeof h) != sizeof h) fail("hello");
    return fd;
}

static void msg(int fd, uint32_t type, uint32_t frames) {
    static char silence[48000 * 4];
    struct mn_audio_msg m = {type, frames * 4};
    if (write(fd, &m, sizeof m) != sizeof m) fail("message");
    if (frames && write(fd, silence, frames * 4) != (ssize_t)frames * 4) fail("frames");
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

int main(void) {
    signal(SIGPIPE, SIG_IGN); /* a stream the host ends fails its write, and the test says so */
    char path[] = "/tmp/mn-audio-test-XXXXXX";
    if (!mkdtemp(path)) fail("mkdtemp");
    static char p[128];
    snprintf(p, sizeof p, "%s/audio.sock", path);
    sock = p;
    if (mn_audio_listen(sock)) fail("listen");
    setvbuf(stdout, NULL, _IONBF, 0);
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
    printf("audio-test: ok\n");
    return 0;
}
