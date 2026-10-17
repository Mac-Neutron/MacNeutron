/* ALSA's "mn" PCM: a game's sound (Wine's winealsa, under FEX) to the Mac, over vsock port MN_AUDIO_PORT to the VM
 * host's CoreAudio player (eac-vm/host/audio.c; protocol mn-audio.h). Playback only; two channels, which games mix
 * down to themselves (a wider device would have ALSA's plug drop the rest); S16 or FLOAT; any rate (CoreAudio
 * converts). Its hardware pointer is the frames the host has played, so the game is paced by the Mac's audio clock.
 * Without the host (no vsock port, or it went away) it plays into nothing at the stream's rate, as the null device
 * did: a game must never fail to open its sound (SMITE 2 crashes on a mastering voice that could not be created).
 * x86-64, against the FEX RootFS's libasound (build-audio.sh); /etc/asound.conf makes it the default device. */
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/vm_sockets.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#include "mn-audio.h"

struct mn {
    snd_pcm_ioplug_t io;
    int fd;                /* the host, or -1 */
    int tried;             /* connecting, since these parameters */
    int timer;             /* without the host: a tick a period, for poll */
    unsigned frame;        /* bytes a frame */
    uint64_t sent, played; /* frames since the connection: sent to the host; played by it (its last word) or the clock */
    uint64_t base;         /* played at the last prepare: the hardware pointer's zero */
    int running;
    double clock_start;    /* without the host: the clock plays from clock_played at clock_start */
    uint64_t clock_played;
    uint8_t heard[64];     /* the host's counts, as they arrive */
    size_t heard_len;
};

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void restart_clock(struct mn *m) { m->clock_start = now(), m->clock_played = m->played; }

/* The host went away: what was out is gone, and the clock plays from here. */
static void lose_host(struct mn *m) {
    close(m->fd), m->fd = -1;
    m->played = m->sent;
    restart_clock(m);
}

static int send_all(struct mn *m, const void *buf, size_t n) {
    for (const char *p = buf; n && m->fd >= 0;) {
        ssize_t k = send(m->fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) lose_host(m);
        else p += k, n -= (size_t)k;
    }
    return m->fd >= 0 ? 0 : -1;
}

static void tell(struct mn *m, uint32_t type) {
    struct mn_audio_msg msg = {type, 0};
    if (m->fd >= 0) send_all(m, &msg, sizeof msg);
}

/* The latest count: the host's (waiting up to timeout_ms for one), or the clock's. */
static void hear(struct mn *m, int timeout_ms) {
    if (m->fd < 0) {
        if (m->running) {
            uint64_t due = m->clock_played + (uint64_t)((now() - m->clock_start) * m->io.rate);
            m->played = due < m->sent ? due : m->sent;
        }
        return;
    }
    struct pollfd p = {.fd = m->fd, .events = POLLIN};
    while (poll(&p, 1, timeout_ms) > 0) {
        ssize_t n = recv(m->fd, m->heard + m->heard_len, sizeof m->heard - m->heard_len, MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
            lose_host(m);
            return;
        }
        if (n > 0) {
            m->heard_len += (size_t)n;
            size_t whole = m->heard_len / 8 * 8;
            uint64_t v;
            if (whole) {
                memcpy(&v, m->heard + whole - 8, 8);
                if (v > m->played) m->played = v;
                memmove(m->heard, m->heard + whole, m->heard_len - whole);
                m->heard_len -= whole;
            }
        }
        timeout_ms = 0;
    }
}

static int connect_host(struct mn *m) {
    int s = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (s < 0) return -1;
    struct sockaddr_vm a = {.svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = MN_AUDIO_PORT};
    int r = connect(s, (struct sockaddr *)&a, sizeof a);
    if (r && errno == EINPROGRESS) {
        struct pollfd p = {.fd = s, .events = POLLOUT};
        int err = 0;
        socklen_t len = sizeof err;
        r = poll(&p, 1, 500) == 1 && !getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) && !err ? 0 : -1;
    }
    if (r) {
        close(s);
        return -1;
    }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
    m->fd = s, m->sent = m->played = m->heard_len = 0;
    struct mn_audio_hello h = {MN_AUDIO_MAGIC, m->io.rate, m->io.channels,
                               m->io.format == SND_PCM_FORMAT_FLOAT_LE ? MN_AUDIO_F32 : MN_AUDIO_S16};
    return send_all(m, &h, sizeof h);
}

/* Whatever is still out is dropped: the host's answer counts it played (a second at most for it). */
static void drop(struct mn *m) {
    hear(m, 0);
    tell(m, MN_AUDIO_STOP);
    for (double end = now() + 1; m->fd >= 0 && m->played < m->sent && now() < end;) hear(m, 50);
    m->played = m->sent, m->running = 0;
}

static int mn_hw_params(snd_pcm_ioplug_t *io, snd_pcm_hw_params_t *params) {
    (void)params;
    struct mn *m = io->private_data;
    m->frame = io->channels * (unsigned)snd_pcm_format_physical_width(io->format) / 8;
    if (m->fd >= 0) close(m->fd), m->fd = -1;  /* the next prepare says hello with these */
    m->tried = 0, m->sent = m->played = 0;
    return 0;
}

static int mn_prepare(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    if (m->fd < 0 && !m->tried) m->tried = 1, connect_host(m);
    drop(m);
    m->base = m->played;
    long ns = (long)(1e9 * io->period_size / io->rate);
    struct itimerspec tick = {{ns / 1000000000, ns % 1000000000}, {ns / 1000000000, ns % 1000000000}};
    timerfd_settime(m->timer, 0, &tick, NULL);
    return 0;
}

static int mn_start(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    tell(m, MN_AUDIO_START);
    m->running = 1;
    restart_clock(m);
    return 0;
}

static int mn_stop(snd_pcm_ioplug_t *io) {
    drop(io->private_data);
    return 0;
}

static int mn_pause(snd_pcm_ioplug_t *io, int enable) {
    struct mn *m = io->private_data;
    hear(m, 0);
    tell(m, enable ? MN_AUDIO_PAUSE : MN_AUDIO_START);
    m->running = !enable;
    restart_clock(m);
    return 0;
}

/* With SND_PCM_IOPLUG_FLAG_BOUNDARY_WA: frames played since the prepare, not wrapped at the buffer's size (a whole
 * buffer played between two calls would look like none). */
static snd_pcm_sframes_t mn_pointer(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    hear(m, 0);
    return (snd_pcm_sframes_t)(m->played - m->base);
}

static snd_pcm_sframes_t mn_transfer(snd_pcm_ioplug_t *io, const snd_pcm_channel_area_t *areas,
                                     snd_pcm_uframes_t offset, snd_pcm_uframes_t size) {
    struct mn *m = io->private_data;
    const char *buf = (const char *)areas->addr + (areas->first + areas->step * offset) / 8;
    struct mn_audio_msg msg = {MN_AUDIO_DATA, (uint32_t)(size * m->frame)};
    if (m->fd >= 0 && !send_all(m, &msg, sizeof msg)) send_all(m, buf, size * m->frame);
    if (m->fd < 0 && m->running) {
        hear(m, 0);
        if (m->played == m->sent) restart_clock(m);  /* it ran dry: these frames play from now, not at once */
    }
    m->sent += size;
    return (snd_pcm_sframes_t)size;
}

static int mn_poll_count(snd_pcm_ioplug_t *io) {
    (void)io;
    return 1;
}

/* The host's counts wake a poll; without it, the timer's ticks. */
static int mn_poll_descriptors(snd_pcm_ioplug_t *io, struct pollfd *pfd, unsigned int space) {
    struct mn *m = io->private_data;
    if (!space) return 0;
    *pfd = (struct pollfd){.fd = m->fd >= 0 ? m->fd : m->timer, .events = POLLIN};
    return 1;
}

static int mn_poll_revents(snd_pcm_ioplug_t *io, struct pollfd *pfd, unsigned int nfds, unsigned short *revents) {
    (void)pfd, (void)nfds;
    struct mn *m = io->private_data;
    uint64_t ticks;
    if (read(m->timer, &ticks, sizeof ticks) < 0) {}  /* the timer's ticks, whichever woke the poll */
    hear(m, 0);
    snd_pcm_sframes_t avail = snd_pcm_avail_update(io->pcm);
    *revents = avail < 0 || avail >= (snd_pcm_sframes_t)io->period_size ? POLLOUT : 0;
    return 0;
}

static int mn_close(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    if (m->fd >= 0) close(m->fd);
    close(m->timer);
    free(m);
    return 0;
}

static const snd_pcm_ioplug_callback_t callbacks = {
    .start = mn_start,
    .stop = mn_stop,
    .pointer = mn_pointer,
    .transfer = mn_transfer,
    .close = mn_close,
    .hw_params = mn_hw_params,
    .prepare = mn_prepare,
    .pause = mn_pause,
    .poll_descriptors_count = mn_poll_count,
    .poll_descriptors = mn_poll_descriptors,
    .poll_revents = mn_poll_revents,
};

static int constrain(snd_pcm_ioplug_t *io) {
    static const unsigned int access[] = {SND_PCM_ACCESS_RW_INTERLEAVED};
    static const unsigned int formats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_FLOAT_LE};
    int err;
    if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS, 1, access)) < 0 ||
        (err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT, 2, formats)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS, 2, 2)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE, 8000, 192000)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, 64, 64 * 1024)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 2, 64)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 256, 256 * 1024)) < 0)
        return err;
    return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(mn) {
    (void)root;
    snd_config_iterator_t i, next;
    snd_config_for_each(i, next, conf) {
        const char *id;
        if (snd_config_get_id(snd_config_iterator_entry(i), &id) < 0) continue;
        if (!strcmp(id, "comment") || !strcmp(id, "type") || !strcmp(id, "hint")) continue;
        SNDERR("Unknown field %s", id);
        return -EINVAL;
    }
    if (stream != SND_PCM_STREAM_PLAYBACK) return -ENOENT;  /* no capture: no microphone in the VM */
    struct mn *m = calloc(1, sizeof *m);
    if (!m) return -ENOMEM;
    m->fd = -1;
    if ((m->timer = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) < 0) {
        free(m);
        return -errno;
    }
    m->io.version = SND_PCM_IOPLUG_VERSION;
    m->io.name = "MacNeutron audio";
    m->io.flags = SND_PCM_IOPLUG_FLAG_BOUNDARY_WA;
    m->io.poll_fd = m->timer;
    m->io.poll_events = POLLIN;
    m->io.callback = &callbacks;
    m->io.private_data = m;
    int err = snd_pcm_ioplug_create(&m->io, name, stream, mode);
    if (err < 0) {
        close(m->timer);
        free(m);
        return err;
    }
    if ((err = constrain(&m->io)) < 0) {
        snd_pcm_ioplug_delete(&m->io);  /* closes, which frees m */
        return err;
    }
    *pcmp = m->io.pcm;
    return 0;
}
SND_PCM_PLUGIN_SYMBOL(mn);
