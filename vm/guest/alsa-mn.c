/* ALSA's "mn" PCM: a game's sound (Wine's winealsa, under FEX) to the Mac, over vsock port MN_AUDIO_PORT to the VM
 * host's CoreAudio player/capture (vm/host/audio.c; protocol mn-audio.h). Playback: two channels, which games mix
 * down to themselves (a wider device would have ALSA's plug drop the rest); S16 or FLOAT; any rate (CoreAudio
 * converts). Capture: 1-2 channels, S16 or FLOAT, 8-192 kHz; connect/prepare never touch host CoreAudio/TCC so
 * probing at game launch completes in microseconds without macOS permission popups. Without the host on playback
 * it plays into nothing at the stream's rate, as the null device did: a game must never fail to open its sound.
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

enum { RX_HDR = 0, RX_BODY = 1 };

struct mn {
    snd_pcm_ioplug_t io;
    int fd;                /* the host, or -1 */
    int tried;             /* connecting, since these parameters */
    int timer;             /* without the host: a tick a period, for poll */
    int is_capture;        /* 0 = playback, 1 = capture */
    unsigned frame;        /* bytes a frame */
    uint64_t sent, played; /* frames since the connection: sent to the host; played by it (its last word) or the clock */
    uint64_t base;         /* played/captured at the last prepare: the hardware pointer's zero */
    int running;
    double clock_start;    /* without the host: the clock plays from clock_played at clock_start */
    uint64_t clock_played;
    uint8_t heard[64];     /* the host's counts, as they arrive */
    size_t heard_len;
    /* Capture state */
    uint8_t *cap_ring;
    uint64_t cap_size;     /* bytes, power of two >= 1 s */
    uint64_t cap_w, cap_r; /* bytes written/read in cap_ring */
    uint64_t captured;     /* complete frames received since the connection */
    int cap_err;
    int rx_state;
    struct mn_audio_msg rx_msg;
    size_t rx_hdr_off;
    uint32_t rx_body_rem;
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

static int connect_vsock(void) {
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
    return s;
}

static int connect_host(struct mn *m) {
    int s = connect_vsock();
    if (s < 0) return -1;
    m->fd = s, m->sent = m->played = m->heard_len = 0;
    /* MNA2: with the buffer, so the host sends the position often enough for it (mn-audio.h). */
    struct {
        struct mn_audio_hello h;
        uint32_t buffer;
    } h = {{MN_AUDIO_MAGIC2, m->io.rate, m->io.channels,
            m->io.format == SND_PCM_FORMAT_FLOAT_LE ? MN_AUDIO_F32 : MN_AUDIO_S16},
           (uint32_t)m->io.buffer_size};
    return send_all(m, &h, sizeof h);
}

static void cap_lose_host(struct mn *m) {
    if (m->fd >= 0) {
        close(m->fd);
        m->fd = -1;
    }
    m->cap_err = 1;
    m->running = 0;
    if (m->timer >= 0) {
        struct itimerspec imm = {{0, 0}, {0, 1}};
        timerfd_settime(m->timer, 0, &imm, NULL);
    }
}

static int cap_send_all(struct mn *m, const void *buf, size_t n) {
    for (const char *p = buf; n && m->fd >= 0;) {
        ssize_t k = send(m->fd, p, n, MSG_NOSIGNAL);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) {
            cap_lose_host(m);
            return -1;
        }
        p += k, n -= (size_t)k;
    }
    return m->fd >= 0 ? 0 : -1;
}

static int connect_cap_host(struct mn *m) {
    int s = connect_vsock();
    if (s < 0) return -1;
    m->fd = s;
    m->cap_w = m->cap_r = 0;
    m->captured = m->base = 0;
    m->cap_err = 0;
    m->rx_state = RX_HDR;
    m->rx_hdr_off = 0;
    m->rx_body_rem = 0;
    struct {
        struct mn_audio_hello h;
        uint32_t buffer;
    } h = {{MN_AUDIO_MAGIC_CAP, m->io.rate, m->io.channels,
            m->io.format == SND_PCM_FORMAT_FLOAT_LE ? MN_AUDIO_F32 : MN_AUDIO_S16},
           (uint32_t)m->io.buffer_size};
    return cap_send_all(m, &h, sizeof h);
}

/* Single nonblocking step of the capture RX state machine.
 * Returns 1 if progress was made, 0 on EAGAIN or status delivery, -1 on error/EOF. */
static int cap_rx_step(struct mn *m, int discard_data, int *got_status, uint32_t *status_code) {
    if (m->rx_state == RX_HDR) {
        ssize_t n = recv(m->fd, (uint8_t *)&m->rx_msg + m->rx_hdr_off,
                         sizeof m->rx_msg - m->rx_hdr_off, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) return 1;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            cap_lose_host(m);
            return -1;
        }
        if (n == 0) {
            cap_lose_host(m);
            return -1;
        }
        m->rx_hdr_off += (size_t)n;
        if (m->rx_hdr_off < sizeof m->rx_msg) return 1;
        m->rx_hdr_off = 0;
        if (m->rx_msg.type == MN_AUDIO_STATUS) {
            if (got_status) {
                *got_status = 1;
                *status_code = m->rx_msg.bytes;
                return 0;
            }
            if (m->rx_msg.bytes != MN_AUDIO_OK) {
                m->cap_err = 1;
                m->running = 0;
                return -1;
            }
            return 1;
        }
        if (m->rx_msg.type == MN_AUDIO_DATA) {
            if (m->rx_msg.bytes > 0) {
                m->rx_state = RX_BODY;
                m->rx_body_rem = m->rx_msg.bytes;
            }
            return 1;
        }
        cap_lose_host(m);
        return -1;
    }

    if (discard_data || !m->cap_ring || !m->cap_size) {
        uint8_t scratch[4096];
        size_t chunk = m->rx_body_rem < sizeof scratch ? m->rx_body_rem : sizeof scratch;
        ssize_t n = recv(m->fd, scratch, chunk, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EINTR) return 1;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            cap_lose_host(m);
            return -1;
        }
        if (n == 0) {
            cap_lose_host(m);
            return -1;
        }
        m->rx_body_rem -= (uint32_t)n;
        if (m->rx_body_rem == 0) m->rx_state = RX_HDR;
        return 1;
    }

    uint64_t at = m->cap_w & (m->cap_size - 1);
    size_t chunk = m->rx_body_rem;
    if (chunk > m->cap_size - at) chunk = (size_t)(m->cap_size - at);
    if (chunk > m->cap_size / 2) chunk = (size_t)(m->cap_size / 2);
    ssize_t n = recv(m->fd, m->cap_ring + at, chunk, MSG_DONTWAIT);
    if (n < 0) {
        if (errno == EINTR) return 1;
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        cap_lose_host(m);
        return -1;
    }
    if (n == 0) {
        cap_lose_host(m);
        return -1;
    }
    unsigned frame = m->frame ? m->frame : 4;
    uint64_t used = m->cap_w - m->cap_r;
    uint64_t free_bytes = m->cap_size - used;
    if ((uint64_t)n > free_bytes) {
        uint64_t need = (uint64_t)n - free_bytes;
        uint64_t drop_frames = (need + frame - 1) / frame;
        uint64_t drop_bytes = drop_frames * frame;
        if (drop_bytes > used) drop_bytes = (used / frame) * frame;
        m->cap_r += drop_bytes;
    }
    uint64_t old_frames = m->cap_w / frame;
    m->cap_w += (uint64_t)n;
    uint64_t new_frames = m->cap_w / frame;
    m->captured += (new_frames - old_frames);
    m->rx_body_rem -= (uint32_t)n;
    if (m->rx_body_rem == 0) m->rx_state = RX_HDR;
    return 1;
}

static void hear_capture(struct mn *m, int blocking) {
    if (m->fd < 0) {
        if (m->running) {
            m->cap_err = 1;
            m->running = 0;
        }
        return;
    }
    if (blocking > 0) {
        struct pollfd p = {.fd = m->fd, .events = POLLIN};
        if (poll(&p, 1, blocking) <= 0) return;
    }
    while (m->fd >= 0 && !m->cap_err) {
        if (cap_rx_step(m, 0, NULL, NULL) <= 0) break;
    }
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
    m->captured = m->base = 0;
    m->cap_w = m->cap_r = 0;
    m->cap_err = 0;
    m->rx_state = RX_HDR;
    m->rx_hdr_off = 0;
    m->rx_body_rem = 0;
    return 0;
}

static int mn_prepare(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    if (m->is_capture) {
        unsigned frame = m->frame ? m->frame : 4;
        uint64_t need = 256 * 1024;
        uint64_t one_sec = (uint64_t)io->rate * frame;
        while (need < one_sec) need <<= 1;
        if (m->cap_size < need || !m->cap_ring) {
            uint8_t *buf = malloc((size_t)need);
            if (!buf) return -ENOMEM;
            free(m->cap_ring);
            m->cap_ring = buf;
            m->cap_size = need;
        }
        if (m->fd >= 0 && m->cap_err) {
            close(m->fd);
            m->fd = -1;
            m->tried = 0;
        }
        if (m->fd < 0 && !m->tried) {
            m->tried = 1;
            connect_cap_host(m);
        } else if (m->fd >= 0) {
            if (m->running) {
                struct mn_audio_msg stop_msg = {MN_AUDIO_STOP, 0};
                cap_send_all(m, &stop_msg, sizeof stop_msg);
            }
            hear_capture(m, 0);
            if (m->fd < 0) {
                connect_cap_host(m);
            } else {
                uint64_t avail_bytes = m->cap_w - m->cap_r;
                m->cap_r += (avail_bytes / frame) * frame;
                m->base = m->captured;
                m->cap_err = 0;
            }
            m->running = 0;
        }
        struct itimerspec disarm = {{0, 0}, {0, 0}};
        timerfd_settime(m->timer, 0, &disarm, NULL);
        return 0;
    }
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
    if (m->is_capture) {
        if (m->fd < 0) {
            m->running = 0;
            m->cap_err = 1;
            return -EIO;
        }
        struct mn_audio_msg msg = {MN_AUDIO_START, 0};
        if (cap_send_all(m, &msg, sizeof msg) < 0) return -EIO;
        double deadline = now() + 0.300;
        uint32_t status_code = MN_AUDIO_ERR_IO;
        int got_status = 0;
        while (!got_status) {
            int r = cap_rx_step(m, 1, &got_status, &status_code);
            if (r < 0 || got_status) break;
            if (r > 0) continue;
            int rem_ms = (int)((deadline - now()) * 1000.0 + 0.999);
            if (rem_ms <= 0) break;
            struct pollfd p = {.fd = m->fd, .events = POLLIN};
            int pr = poll(&p, 1, rem_ms);
            if (pr < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (pr == 0) break;
        }
        if (!got_status) {
            cap_lose_host(m);
            return -EIO;
        }
        if (status_code != MN_AUDIO_OK) {
            m->running = 0;
            m->cap_err = 1;
            return -EIO;
        }
        m->cap_w = m->cap_r = 0;
        m->base = m->captured;
        m->running = 1;
        m->cap_err = 0;
        return 0;
    }
    tell(m, MN_AUDIO_START);
    m->running = 1;
    restart_clock(m);
    return 0;
}

static int mn_stop(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    if (m->is_capture) {
        unsigned frame = m->frame ? m->frame : 4;
        if (m->fd >= 0) {
            struct mn_audio_msg msg = {MN_AUDIO_STOP, 0};
            cap_send_all(m, &msg, sizeof msg);
            hear_capture(m, 0);
        }
        uint64_t avail_bytes = m->cap_w - m->cap_r;
        m->cap_r += (avail_bytes / frame) * frame;
        m->base = m->captured;
        m->running = 0;
        return 0;
    }
    drop(m);
    return 0;
}

static int mn_pause(snd_pcm_ioplug_t *io, int enable) {
    struct mn *m = io->private_data;
    if (m->is_capture) {
        if (!enable) return mn_start(io);
        hear_capture(m, 0);
        if (m->fd >= 0) {
            struct mn_audio_msg msg = {MN_AUDIO_PAUSE, 1u};
            cap_send_all(m, &msg, sizeof msg);
        }
        m->running = 0;
        return m->cap_err ? -EIO : 0;
    }
    hear(m, 0);
    tell(m, enable ? MN_AUDIO_PAUSE : MN_AUDIO_START);
    m->running = !enable;
    restart_clock(m);
    return 0;
}

/* With SND_PCM_IOPLUG_FLAG_BOUNDARY_WA: frames played/captured since the prepare, not wrapped at the buffer's size
 * (a whole buffer between two calls would look like none). */
static snd_pcm_sframes_t mn_pointer(snd_pcm_ioplug_t *io) {
    struct mn *m = io->private_data;
    if (m->is_capture) {
        hear_capture(m, 0);
        if (m->cap_err) return -EIO;
        return (snd_pcm_sframes_t)(m->captured - m->base);
    }
    hear(m, 0);
    return (snd_pcm_sframes_t)(m->played - m->base);
}

static snd_pcm_sframes_t mn_transfer(snd_pcm_ioplug_t *io, const snd_pcm_channel_area_t *areas,
                                     snd_pcm_uframes_t offset, snd_pcm_uframes_t size) {
    struct mn *m = io->private_data;
    if (m->is_capture) {
        hear_capture(m, 0);
        unsigned frame = m->frame ? m->frame : 4;
        uint64_t avail_frames = (m->cap_w - m->cap_r) / frame;
        if (m->cap_err && avail_frames == 0) return -EIO;
        snd_pcm_uframes_t todo = size < avail_frames ? size : (snd_pcm_uframes_t)avail_frames;
        if (!todo) return 0;
        uint64_t bytes = (uint64_t)todo * frame;
        uint64_t at = m->cap_r & (m->cap_size - 1);
        uint64_t first = bytes < m->cap_size - at ? bytes : m->cap_size - at;
        uint8_t *dst = (uint8_t *)areas->addr + (areas->first + areas->step * offset) / 8;
        memcpy(dst, m->cap_ring + at, (size_t)first);
        if (bytes > first) memcpy(dst + first, m->cap_ring, (size_t)(bytes - first));
        m->cap_r += bytes;
        return (snd_pcm_sframes_t)todo;
    }
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
    if (m->is_capture) {
        hear_capture(m, 0);
        if (m->cap_err) {
            *revents = POLLERR;
            return 0;
        }
        unsigned frame = m->frame ? m->frame : 4;
        uint64_t avail_frames = (m->cap_w - m->cap_r) / frame;
        *revents = avail_frames >= io->period_size ? POLLIN : 0;
        return 0;
    }
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
    free(m->cap_ring);
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
    struct mn *m = io->private_data;
    static const unsigned int access[] = {SND_PCM_ACCESS_RW_INTERLEAVED};
    static const unsigned int formats[] = {SND_PCM_FORMAT_S16_LE, SND_PCM_FORMAT_FLOAT_LE};
    unsigned int min_ch = m->is_capture ? 1 : 2;
    int err;
    if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS, 1, access)) < 0 ||
        (err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT, 2, formats)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS, min_ch, 2)) < 0 ||
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
    if (stream != SND_PCM_STREAM_PLAYBACK && stream != SND_PCM_STREAM_CAPTURE) return -ENOENT;
    struct mn *m = calloc(1, sizeof *m);
    if (!m) return -ENOMEM;
    m->fd = -1;
    m->is_capture = (stream == SND_PCM_STREAM_CAPTURE);
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
