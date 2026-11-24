/* The guest's sound (spec §5.7): every connection to the audio socket (the guest's vsock port MN_AUDIO_PORT, from
 * vm/guest/alsa-mn.c; protocol vm/guest/mn-audio.h) is one ALSA stream, played through its own CoreAudio
 * output unit (or captured through a TCC-isolated HALOutput unit for MNAC streams). A second of the stream's frames
 * (and at least the plugin's largest ALSA buffer) sits in a ring between the connection's thread and the output; what
 * the output takes is what the guest's ALSA sees as played.
 * The connection's thread never stops reading: libkrun's vsock proxy sends to it with blocking calls, from the thread
 * every vsock connection and the console share. So each stream's output has a thread of its own (play), which makes
 * the unit, and takes frames at the stream's rate into nothing until there is one, so a game never waits on its
 * sound: no device, CoreAudio not answering yet (a stuck device driver can stall every client's calls, so it is asked
 * off the streams' threads, until it answers), or MN_AUDIO_OUTPUT=none (tests; =slow makes each output take 3 s). */
#include "audio.h"
#include "audio_tcc.h"
#include "../guest/mn-audio.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* How often a connection reports the position while it waits (Direct3D forwarding M5): ALSA reads it about once a
 * period, so every 1-2 ms woke the connection about 800 times a second for nothing (audio-test's calm). A quarter of
 * the guest's buffer (an MNA2 hello says it), REPORT_MS at most: Wine's default 40 ms buffer gets 10 ms, its shortest
 * (four 5 ms periods) 5 ms, so a late position never leaves the output less than a CoreAudio cycle (about 10.7 ms). */
#define REPORT_MS 10
#define CAP_START_TIMEOUT_MS 200

static _Atomic unsigned g_streams = 0;

/* Focus mute state: starts active (unmuted) for headless tests & pre-window boot. */
static _Atomic int g_focus_active = 1;
static _Atomic int g_mute_in_background = 1;
static pthread_once_t g_mute_once = PTHREAD_ONCE_INIT;

static void init_mute_env(void) {
    const char *v = getenv("MN_AUDIO_MUTE_BACKGROUND");
    if (v && !strcmp(v, "0")) atomic_store_explicit(&g_mute_in_background, 0, memory_order_release);
}

static void ensure_mute_env(void) {
    pthread_once(&g_mute_once, init_mute_env);
}

void mn_audio_set_focus_active(int active) {
    atomic_store_explicit(&g_focus_active, active ? 1 : 0, memory_order_release);
}

void mn_audio_set_mute_in_background(int enabled) {
    ensure_mute_env();
    atomic_store_explicit(&g_mute_in_background, enabled ? 1 : 0, memory_order_release);
}

int mn_audio_mute_in_background_enabled(void) {
    ensure_mute_env();
    return atomic_load_explicit(&g_mute_in_background, memory_order_acquire);
}

float mn_audio_apply_fade(uint8_t *buf, uint64_t frames, unsigned channels, int f32, unsigned rate, float phase,
                          int want_mute) {
    if (phase < 0.0f) phase = 0.0f;
    if (phase > 1.0f) phase = 1.0f;
    if (!buf || frames == 0 || channels == 0) return phase;
    if (phase >= 1.0f && !want_mute) return 1.0f;
    size_t frame_bytes = (size_t)channels * (f32 ? sizeof(float) : sizeof(int16_t));
    if (phase <= 0.0f && want_mute) {
        memset(buf, 0, (size_t)frames * frame_bytes);
        return 0.0f;
    }
    if (rate == 0) rate = 48000;
    double p = (double)phase;
    double dp = 1.0 / ((double)rate * 0.150);
    double snap = 0.5 * dp;
    for (uint64_t i = 0; i < frames; i++) {
        if (want_mute) {
            p -= dp;
            if (p <= snap) {
                memset(buf + i * frame_bytes, 0, (size_t)(frames - i) * frame_bytes);
                p = 0.0;
                break;
            }
        } else {
            p += dp;
            if (p >= 1.0 - snap) {
                p = 1.0;
                break;
            }
        }
        float g = (float)(p * p * (3.0 - 2.0 * p));
        if (f32) {
            float *fp = (float *)(buf + i * frame_bytes);
            for (unsigned c = 0; c < channels; c++) fp[c] *= g;
        } else {
            int16_t *sp = (int16_t *)(buf + i * frame_bytes);
            for (unsigned c = 0; c < channels; c++) {
                long v = lrintf((float)sp[c] * g);
                if (v < -32768) v = -32768;
                else if (v > 32767) v = 32767;
                sp[c] = (int16_t)v;
            }
        }
    }
    return (float)p;
}

struct stream {
    unsigned id, rate, channels, frame; /* frame: bytes a frame */
    int f32;
    float fade_phase;             /* owned exclusively by render() thread */
    uint8_t *ring;
    uint64_t size;                /* bytes, a power of two */
    _Atomic uint64_t w, r;        /* bytes queued by the connection; taken (played or dropped) by the output */
    _Atomic uint64_t drop_to;     /* a STOP's mark: the output drops what was queued before it */
    _Atomic uint64_t dropped;     /* bytes */
    _Atomic int playing, closing;
    _Atomic int refs;             /* the connection's thread and the output's */
    uint64_t turns, messages, reports; /* the connection's wake-ups, for its last line (Direct3D forwarding M5) */
    int report_ms;
};

static void put(struct stream *s) {
    if (atomic_fetch_sub(&s->refs, 1) == 1) {
        free(s->ring);
        free(s);
    }
}

/* The output's side (one thread at a time: the unit's render callback or play's): whole frames from the ring into dst
 * (if any), silence for the rest, after dropping what a STOP marked. */
static void take(struct stream *s, uint8_t *dst, uint64_t want) {
    uint64_t mark = atomic_load_explicit(&s->drop_to, memory_order_acquire); /* before w: a mark is never past it */
    uint64_t w = atomic_load_explicit(&s->w, memory_order_acquire), got = 0;
    uint64_t r = atomic_load_explicit(&s->r, memory_order_relaxed);
    if (r < mark) {
        atomic_fetch_add_explicit(&s->dropped, mark - r, memory_order_relaxed);
        r = mark;
    }
    if (atomic_load_explicit(&s->playing, memory_order_relaxed)) {
        got = w - r < want ? w - r : want;
        got -= got % s->frame;
        uint64_t at = r & (s->size - 1), first = got < s->size - at ? got : s->size - at;
        if (dst) memcpy(dst, s->ring + at, first), memcpy(dst + first, s->ring, got - first);
    }
    if (dst) memset(dst + got, 0, want - got);
    atomic_store_explicit(&s->r, r + got, memory_order_release);
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
                       UInt32 frames, AudioBufferList *out) {
    (void)flags, (void)time, (void)bus, (void)frames;
    struct stream *s = ref;
    uint8_t *dst = out->mBuffers[0].mData;
    uint64_t want = out->mBuffers[0].mDataByteSize;
    int want_mute = !atomic_load_explicit(&g_focus_active, memory_order_relaxed) &&
                    atomic_load_explicit(&g_mute_in_background, memory_order_relaxed);
    take(s, (s->fade_phase <= 0.0f && want_mute) ? NULL : dst, want);
    s->fade_phase = mn_audio_apply_fade(dst, want / s->frame, s->channels, s->f32, s->rate, s->fade_phase, want_mute);
    return noErr;
}

static const AudioComponentDescription default_output = {.componentType = kAudioUnitType_Output,
                                                         .componentSubType = kAudioUnitSubType_DefaultOutput,
                                                         .componentManufacturer = kAudioUnitManufacturer_Apple};
static _Atomic int answered; /* CoreAudio made an output once */

static void *ask_coreaudio(void *arg) {
    (void)arg;
    for (int asked = 0;; asked++, sleep(5)) {
        AudioComponent c = AudioComponentFindNext(NULL, &default_output);
        AudioUnit u;
        if (c && !AudioComponentInstanceNew(c, &u)) {
            AudioComponentInstanceDispose(u);
            atomic_store(&answered, 1);
            return NULL;
        }
        if (!asked) fprintf(stderr, "mn-audio: no audio output yet\n");
    }
}

/* The stream's CoreAudio unit, started; NULL until CoreAudio has answered, or if it refuses. */
static AudioUnit output(struct stream *s) {
    AudioComponent c = atomic_load(&answered) ? AudioComponentFindNext(NULL, &default_output) : NULL;
    AudioUnit u = NULL;
    if (!c || AudioComponentInstanceNew(c, &u)) return NULL;
    AudioStreamBasicDescription f = {
        .mSampleRate = s->rate, .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = (s->f32 ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger) | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = s->frame, .mFramesPerPacket = 1, .mBytesPerFrame = s->frame,
        .mChannelsPerFrame = s->channels, .mBitsPerChannel = s->f32 ? 32 : 16,
    };
    AURenderCallbackStruct cb = {render, s};
    if (AudioUnitSetProperty(u, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) ||
        AudioUnitSetProperty(u, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &cb, sizeof cb) ||
        AudioUnitInitialize(u) || AudioOutputUnitStart(u)) {
        AudioComponentInstanceDispose(u);
        return NULL;
    }
    return u;
}

/* The stream's output for its life: the unit once there is one (asked again each second), the frames taken at the
 * stream's rate into nothing until then. */
static void *play(void *arg) {
    struct stream *s = arg;
    const char *mode = getenv("MN_AUDIO_OUTPUT");
    int none = mode && !strcmp(mode, "none");
    if (mode && !strcmp(mode, "slow")) sleep(3);
    AudioUnit unit = none ? NULL : output(s);
    if (!unit) fprintf(stderr, "mn-audio: stream %u: no output yet: played into nothing\n", s->id);
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (uint64_t taken = 0, tick = 1; !unit && !atomic_load(&s->closing); tick++) {
        usleep(4000);
        clock_gettime(CLOCK_MONOTONIC, &t);
        uint64_t due = (uint64_t)(((double)(t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9) * s->rate);
        take(s, NULL, (due - taken) * s->frame);
        taken = due;
        if (!none && tick % 200 == 0 && (unit = output(s)))
            fprintf(stderr, "mn-audio: stream %u: on the Mac's output now\n", s->id);
    }
    while (unit && !atomic_load(&s->closing)) usleep(10000);
    if (unit) {
        AudioOutputUnitStop(unit);
        AudioUnitUninitialize(unit);
        AudioComponentInstanceDispose(unit);
    }
    put(s);
    return NULL;
}

static int read_all(int fd, void *buf, size_t n) {
    for (uint8_t *p = buf; n;) {
        ssize_t got = read(fd, p, n);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        p += got, n -= (size_t)got;
    }
    return 0;
}

/* The guest's hardware pointer: frames taken so far, sent when they changed. */
static int report(int fd, struct stream *s, uint64_t *told) {
    uint64_t played = atomic_load_explicit(&s->r, memory_order_acquire) / s->frame;
    if (played == *told) return 0;
    *told = played;
    s->reports++;
    return send(fd, &played, sizeof played, 0) == sizeof played ? 0 : -1;
}

/* A DATA message's frames into the ring. The guest never has more than its ALSA buffer out (at most 256 KiB, which
 * the ring holds), so waiting for room is only ever brief. */
static int queue(int fd, struct stream *s, uint32_t bytes, uint64_t *told) {
    while (bytes) {
        uint64_t w = atomic_load_explicit(&s->w, memory_order_relaxed);
        uint64_t room = s->size - (w - atomic_load_explicit(&s->r, memory_order_acquire));
        if (!room) {
            usleep((useconds_t)s->report_ms * 1000);
            s->turns++;
            if (report(fd, s, told)) return -1;
            continue;
        }
        uint64_t at = w & (s->size - 1), n = bytes < room ? bytes : room;
        if (n > s->size - at) n = s->size - at;
        ssize_t got = read(fd, s->ring + at, n);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return -1;
        atomic_store_explicit(&s->w, w + (uint64_t)got, memory_order_release);
        bytes -= (uint32_t)got;
    }
    return 0;
}

/* --- 200 ms TCC-Isolated Microphone Capture (MNAC) --- */

#define CAP_CONV_NO_DATA ((OSStatus)0x6d6f7265) /* 'more' */
#define CAP_TX_CHUNK 16384
#define CAP_TX_CAP (sizeof(struct mn_audio_msg) + CAP_TX_CHUNK + sizeof(struct mn_audio_msg))

enum cap_mode {
    CAP_MODE_REAL = 0,
    CAP_MODE_HANG = 1,
    CAP_MODE_SINE = 2,
};

struct cap_ring {
    uint8_t *buf;
    uint64_t size;              /* power-of-two >= 1s of guest audio, capped at 4 MiB */
    unsigned frame;             /* bytes per frame */
    _Atomic uint64_t w, r;      /* wait-free SPSC indices in bytes (frame-aligned) */
    _Atomic int active;         /* 1 while producer may push, 0 on stop/pause/cancel/close */
    _Atomic int refs;
};

struct cap_stream {
    unsigned id, rate, channels, frame;
    int f32;
    uint32_t buffer_frames;
    uint64_t ring_size;
    int wake_rd, wake_wr;
    struct cap_ring *active_ring; /* owned exclusively by serve_capture thread */
    _Atomic uint64_t gen;         /* incremented on START/PAUSE/STOP; invalidates prior sessions */
    _Atomic int closing;
    _Atomic int refs;
    uint64_t sent_bytes;
    uint8_t tx_buf[CAP_TX_CAP];
    size_t tx_off, tx_len;
};

struct cap_start_job {
    struct cap_stream *s;       /* owns +1 ref on s */
    struct cap_ring *ring;      /* owns +1 ref on ring (for CAP_MODE_SINE / CAP_MODE_REAL) */
    dispatch_semaphore_t done;
    uint64_t gen;
    enum cap_mode mode;
    int auth;
    _Atomic int state;          /* 0=pending, 1=cancelled(timeout), 2=committed */
    _Atomic int status;         /* MN_AUDIO_OK or MN_AUDIO_ERR_* */
    _Atomic int refs;           /* 2: serve_capture + cap_worker */
};

struct cap_session {
    struct cap_stream *s;
    struct cap_ring *ring;      /* borrowed from job while cap_worker owns job */
    uint64_t gen;
    AudioUnit unit;
    AudioConverterRef conv;
    AudioStreamBasicDescription dev_asbd;
    AudioStreamBasicDescription guest_asbd;
    uint8_t *hw_scratch;
    uint32_t hw_scratch_bytes;
    uint8_t *conv_scratch;
    uint32_t conv_scratch_bytes;
    const uint8_t *conv_src;
    uint32_t conv_src_frames;
};

static struct cap_ring *cap_ring_new(uint64_t size, unsigned frame) {
    struct cap_ring *ring = calloc(1, sizeof *ring);
    if (!ring) return NULL;
    ring->buf = malloc((size_t)size);
    if (!ring->buf) {
        free(ring);
        return NULL;
    }
    ring->size = size;
    ring->frame = frame;
    atomic_init(&ring->w, 0);
    atomic_init(&ring->r, 0);
    atomic_init(&ring->active, 0);
    atomic_init(&ring->refs, 1);
    return ring;
}

static struct cap_ring *cap_ring_retain(struct cap_ring *ring) {
    if (ring) atomic_fetch_add_explicit(&ring->refs, 1, memory_order_relaxed);
    return ring;
}

static void cap_ring_put(struct cap_ring *ring) {
    if (ring && atomic_fetch_sub_explicit(&ring->refs, 1, memory_order_acq_rel) == 1) {
        free(ring->buf);
        free(ring);
    }
}

static void cap_put(struct cap_stream *s) {
    if (atomic_fetch_sub_explicit(&s->refs, 1, memory_order_acq_rel) == 1) {
        if (s->wake_rd >= 0) close(s->wake_rd);
        if (s->wake_wr >= 0) close(s->wake_wr);
        cap_ring_put(s->active_ring);
        free(s);
    }
}

static void cap_job_put(struct cap_start_job *job) {
    if (atomic_fetch_sub_explicit(&job->refs, 1, memory_order_acq_rel) == 1) {
        cap_ring_put(job->ring);
        if (job->s) cap_put(job->s);
        if (job->done) dispatch_release(job->done);
        free(job);
    }
}

/* Wait-free non-overwriting SPSC producer push: drops overflow incoming frames instead of touching reader bytes. */
static void cap_ring_push(struct cap_stream *s, struct cap_ring *ring, const uint8_t *src, uint64_t bytes) {
    if (!ring || !bytes || !atomic_load_explicit(&ring->active, memory_order_acquire)) return;
    uint64_t w = atomic_load_explicit(&ring->w, memory_order_relaxed);
    uint64_t r = atomic_load_explicit(&ring->r, memory_order_acquire);
    uint64_t used = w - r;
    if (used >= ring->size) return;
    uint64_t room = ring->size - used;
    uint64_t to_write = bytes < room ? bytes : room;
    to_write -= to_write % ring->frame;
    if (!to_write) return;
    uint64_t at = w & (ring->size - 1);
    uint64_t first = to_write < ring->size - at ? to_write : ring->size - at;
    memcpy(ring->buf + at, src, (size_t)first);
    if (to_write > first) memcpy(ring->buf, src + first, (size_t)(to_write - first));
    atomic_store_explicit(&ring->w, w + to_write, memory_order_release);
    char wake = 1;
    (void)write(s->wake_wr, &wake, 1);
}

static OSStatus cap_conv_data_proc(AudioConverterRef conv, UInt32 *io_num_packets, AudioBufferList *io_data,
                                   AudioStreamPacketDescription **out_pkt_desc, void *user_data) {
    (void)conv;
    if (out_pkt_desc) *out_pkt_desc = NULL;
    struct cap_session *sess = user_data;
    if (sess->conv_src_frames == 0) {
        *io_num_packets = 0;
        io_data->mBuffers[0].mDataByteSize = 0;
        io_data->mBuffers[0].mData = NULL;
        return CAP_CONV_NO_DATA;
    }
    UInt32 take_frames = *io_num_packets < sess->conv_src_frames ? *io_num_packets : sess->conv_src_frames;
    io_data->mBuffers[0].mNumberChannels = sess->dev_asbd.mChannelsPerFrame;
    io_data->mBuffers[0].mData = (void *)sess->conv_src;
    io_data->mBuffers[0].mDataByteSize = take_frames * sess->dev_asbd.mBytesPerFrame;
    *io_num_packets = take_frames;
    sess->conv_src += take_frames * sess->dev_asbd.mBytesPerFrame;
    sess->conv_src_frames -= take_frames;
    return noErr;
}

static OSStatus cap_input_cb(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
                             UInt32 frames, AudioBufferList *io_data) {
    (void)io_data;
    struct cap_session *sess = ref;
    struct cap_stream *s = sess->s;
    struct cap_ring *ring = sess->ring;
    if (!ring || !atomic_load_explicit(&ring->active, memory_order_acquire) ||
        atomic_load_explicit(&s->closing, memory_order_relaxed) ||
        atomic_load_explicit(&s->gen, memory_order_relaxed) != sess->gen) {
        return noErr;
    }
    if (frames == 0) return noErr;
    uint32_t max_in = sess->hw_scratch_bytes / sess->dev_asbd.mBytesPerFrame;
    if (frames > max_in) frames = max_in;
    if (frames == 0) return noErr;

    AudioBufferList in_abl = {
        .mNumberBuffers = 1,
        .mBuffers = {{
            .mNumberChannels = sess->dev_asbd.mChannelsPerFrame,
            .mDataByteSize = frames * sess->dev_asbd.mBytesPerFrame,
            .mData = sess->hw_scratch,
        }},
    };
    OSStatus err = AudioUnitRender(sess->unit, flags, time, bus, frames, &in_abl);
    if (err != noErr) return err;

    sess->conv_src = sess->hw_scratch;
    sess->conv_src_frames = frames;
    for (;;) {
        UInt32 out_frames = sess->conv_scratch_bytes / s->frame;
        AudioBufferList out_abl = {
            .mNumberBuffers = 1,
            .mBuffers = {{
                .mNumberChannels = s->channels,
                .mDataByteSize = out_frames * s->frame,
                .mData = sess->conv_scratch,
            }},
        };
        OSStatus cerr = AudioConverterFillComplexBuffer(sess->conv, cap_conv_data_proc, sess, &out_frames, &out_abl, NULL);
        if (out_frames > 0) cap_ring_push(s, ring, sess->conv_scratch, (uint64_t)out_frames * s->frame);
        if (cerr == CAP_CONV_NO_DATA || cerr != noErr || out_frames == 0) break;
    }
    return noErr;
}

static void cap_session_destroy(struct cap_session *sess, int started) {
    if (!sess) return;
    if (sess->unit) {
        if (started) AudioOutputUnitStop(sess->unit);
        AudioUnitUninitialize(sess->unit);
        AudioComponentInstanceDispose(sess->unit);
    }
    if (sess->conv) AudioConverterDispose(sess->conv);
    free(sess->hw_scratch);
    free(sess->conv_scratch);
    free(sess);
}

static int cap_commit_job(struct cap_start_job *job, int status) {
    int expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&job->state, &expected, 2, memory_order_acq_rel,
                                               memory_order_acquire)) {
        return 0; /* caller already timed out and cancelled */
    }
    atomic_store_explicit(&job->status, status, memory_order_release);
    if (status == MN_AUDIO_OK && job->ring) atomic_store_explicit(&job->ring->active, 1, memory_order_release);
    dispatch_semaphore_signal(job->done);
    return 1;
}

static int cap_is_aborted(struct cap_start_job *job) {
    struct cap_stream *s = job->s;
    return atomic_load_explicit(&job->state, memory_order_acquire) == 1 ||
           atomic_load_explicit(&s->closing, memory_order_acquire) ||
           atomic_load_explicit(&s->gen, memory_order_acquire) != job->gen;
}

static void run_sine_loop(struct cap_stream *s, struct cap_ring *ring, uint64_t gen) {
    uint8_t buf[512 * 8];
    double phase = 0.0;
    const double phase_inc = 2.0 * M_PI * 440.0 / (double)s->rate;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t generated = 0;
    while (!atomic_load_explicit(&s->closing, memory_order_acquire) &&
           atomic_load_explicit(&s->gen, memory_order_acquire) == gen &&
           atomic_load_explicit(&ring->active, memory_order_acquire)) {
        usleep(5000);
        if (atomic_load_explicit(&s->closing, memory_order_acquire) ||
            atomic_load_explicit(&s->gen, memory_order_acquire) != gen ||
            !atomic_load_explicit(&ring->active, memory_order_acquire)) {
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &t);
        uint64_t due = (uint64_t)(((double)(t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9) * s->rate);
        while (generated < due) {
            uint64_t n = due - generated;
            if (n > 512) n = 512;
            for (uint64_t i = 0; i < n; i++) {
                double val = 0.25 * sin(phase);
                phase += phase_inc;
                if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
                if (s->f32) {
                    float *fp = (float *)(buf + i * s->frame);
                    float fv = (float)val;
                    for (unsigned c = 0; c < s->channels; c++) fp[c] = fv;
                } else {
                    int16_t *sp = (int16_t *)(buf + i * s->frame);
                    int16_t sv = (int16_t)lrint(val * 32767.0);
                    for (unsigned c = 0; c < s->channels; c++) sp[c] = sv;
                }
            }
            cap_ring_push(s, ring, buf, n * s->frame);
            generated += n;
        }
    }
}

static void *cap_worker(void *arg) {
    struct cap_start_job *job = arg;
    struct cap_stream *s = job->s;
    uint64_t gen = job->gen;

    if (job->mode == CAP_MODE_HANG) {
        sleep(5);
        /* If caller timed out (expected), cleanly discard without streaming a frame. */
        cap_commit_job(job, MN_AUDIO_ERR_TIMEOUT);
        cap_job_put(job);
        return NULL;
    }

    if (job->mode == CAP_MODE_SINE) {
        if (!cap_is_aborted(job) && cap_commit_job(job, MN_AUDIO_OK)) {
            run_sine_loop(s, job->ring, gen);
        }
        cap_job_put(job);
        return NULL;
    }

    /* Real CoreAudio HALOutput + AudioConverter capture path. */
    AudioDeviceID dev_id = kAudioObjectUnknown;
    UInt32 prop_sz = sizeof dev_id;
    AudioObjectPropertyAddress dev_addr = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &dev_addr, 0, NULL, &prop_sz, &dev_id) != noErr ||
        dev_id == kAudioObjectUnknown) {
        cap_commit_job(job, MN_AUDIO_ERR_NODEV);
        cap_job_put(job);
        return NULL;
    }

    AudioObjectPropertyAddress cfg_addr = {
        kAudioDevicePropertyStreamConfiguration,
        kAudioDevicePropertyScopeInput,
        kAudioObjectPropertyElementMain,
    };
    UInt32 cfg_sz = 0;
    if (AudioObjectGetPropertyDataSize(dev_id, &cfg_addr, 0, NULL, &cfg_sz) != noErr ||
        cfg_sz < sizeof(AudioBufferList)) {
        cap_commit_job(job, MN_AUDIO_ERR_NODEV);
        cap_job_put(job);
        return NULL;
    }
    AudioBufferList *abl = malloc(cfg_sz);
    UInt32 input_ch = 0;
    if (abl && AudioObjectGetPropertyData(dev_id, &cfg_addr, 0, NULL, &cfg_sz, abl) == noErr) {
        for (UInt32 i = 0; i < abl->mNumberBuffers; i++) input_ch += abl->mBuffers[i].mNumberChannels;
    }
    free(abl);
    if (input_ch == 0) {
        cap_commit_job(job, MN_AUDIO_ERR_NODEV);
        cap_job_put(job);
        return NULL;
    }

    char dev_desc[128] = "unknown";
    mn_audio_describe_input_device(dev_id, dev_desc, sizeof dev_desc);
    fprintf(stderr, "mn-audio: capture stream %u: device='%s' tcc=%s\n", s->id, dev_desc,
            job->auth == MN_MIC_AUTHORIZED ? "authorized" : "not_determined");

    if (cap_is_aborted(job)) {
        cap_job_put(job);
        return NULL;
    }

    struct cap_session *sess = calloc(1, sizeof *sess);
    if (!sess) {
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }
    sess->s = s;
    sess->ring = job->ring;
    sess->gen = gen;

    static const AudioComponentDescription hal_desc = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_HALOutput,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent comp = AudioComponentFindNext(NULL, &hal_desc);
    UInt32 one = 1, zero = 0;
    if (!comp || AudioComponentInstanceNew(comp, &sess->unit) != noErr ||
        AudioUnitSetProperty(sess->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1, &one,
                             sizeof one) != noErr ||
        AudioUnitSetProperty(sess->unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &zero,
                             sizeof zero) != noErr ||
        AudioUnitSetProperty(sess->unit, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0, &dev_id,
                             sizeof dev_id) != noErr) {
        cap_session_destroy(sess, 0);
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }

    AudioStreamBasicDescription hw_asbd = {0};
    UInt32 asbd_sz = sizeof hw_asbd;
    if (AudioUnitGetProperty(sess->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 1, &hw_asbd,
                             &asbd_sz) != noErr) {
        cap_session_destroy(sess, 0);
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }

    UInt32 hw_ch = hw_asbd.mChannelsPerFrame ? hw_asbd.mChannelsPerFrame : input_ch;
    sess->dev_asbd = (AudioStreamBasicDescription){
        .mSampleRate = hw_asbd.mSampleRate > 0 ? hw_asbd.mSampleRate : 48000.0,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = hw_ch * (UInt32)sizeof(Float32),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = hw_ch * (UInt32)sizeof(Float32),
        .mChannelsPerFrame = hw_ch,
        .mBitsPerChannel = 32,
    };
    sess->guest_asbd = (AudioStreamBasicDescription){
        .mSampleRate = (Float64)s->rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = (s->f32 ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger) | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = s->frame,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = s->frame,
        .mChannelsPerFrame = s->channels,
        .mBitsPerChannel = s->f32 ? 32 : 16,
    };

    if (AudioUnitSetProperty(sess->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &sess->dev_asbd,
                             sizeof sess->dev_asbd) != noErr ||
        AudioConverterNew(&sess->dev_asbd, &sess->guest_asbd, &sess->conv) != noErr) {
        cap_session_destroy(sess, 0);
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }

    if (sess->dev_asbd.mChannelsPerFrame != sess->guest_asbd.mChannelsPerFrame) {
        SInt32 ch_map[2] = {0, sess->dev_asbd.mChannelsPerFrame > 1 ? 1 : 0};
        AudioConverterSetProperty(sess->conv, kAudioConverterChannelMap,
                                  (UInt32)(s->channels * sizeof(SInt32)), ch_map);
    }

    UInt32 max_frames = 4096;
    UInt32 mf_sz = sizeof max_frames;
    if (AudioUnitGetProperty(sess->unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0,
                             &max_frames, &mf_sz) != noErr ||
        max_frames < 4096) {
        max_frames = 4096;
    }
    if (max_frames > 16384) max_frames = 16384;

    uint32_t max_out_frames = (uint32_t)ceil((double)max_frames * (double)s->rate / sess->dev_asbd.mSampleRate) + 64;
    sess->hw_scratch_bytes = max_frames * sess->dev_asbd.mBytesPerFrame;
    sess->conv_scratch_bytes = max_out_frames * s->frame;
    sess->hw_scratch = malloc(sess->hw_scratch_bytes);
    sess->conv_scratch = malloc(sess->conv_scratch_bytes);
    if (!sess->hw_scratch || !sess->conv_scratch) {
        cap_session_destroy(sess, 0);
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }

    AURenderCallbackStruct cb = {cap_input_cb, sess};
    if (AudioUnitSetProperty(sess->unit, kAudioOutputUnitProperty_SetInputCallback, kAudioUnitScope_Global, 0, &cb,
                             sizeof cb) != noErr ||
        AudioUnitInitialize(sess->unit) != noErr) {
        cap_session_destroy(sess, 0);
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }

    if (cap_is_aborted(job)) {
        cap_session_destroy(sess, 0);
        cap_job_put(job);
        return NULL;
    }

    if (AudioOutputUnitStart(sess->unit) != noErr) {
        cap_session_destroy(sess, 0);
        cap_commit_job(job, MN_AUDIO_ERR_IO);
        cap_job_put(job);
        return NULL;
    }

    /* Check cancellation atomically before publishing capturing=1; if 200 ms elapsed, stop & tear down without
     * streaming a single frame. */
    if (atomic_load_explicit(&s->closing, memory_order_acquire) ||
        atomic_load_explicit(&s->gen, memory_order_acquire) != gen ||
        !cap_commit_job(job, MN_AUDIO_OK)) {
        cap_session_destroy(sess, 1);
        cap_job_put(job);
        return NULL;
    }

    while (!atomic_load_explicit(&s->closing, memory_order_acquire) &&
           atomic_load_explicit(&s->gen, memory_order_acquire) == gen) {
        usleep(10000);
    }

    cap_session_destroy(sess, 1);
    cap_job_put(job);
    return NULL;
}

static int cap_flush_tx(int fd, struct cap_stream *s) {
    while (s->tx_off < s->tx_len) {
        ssize_t sent = send(fd, s->tx_buf + s->tx_off, s->tx_len - s->tx_off, MSG_DONTWAIT);
        if (sent < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        if (sent == 0) return -1;
        s->tx_off += (size_t)sent;
    }
    s->tx_off = s->tx_len = 0;
    return 0;
}

static void cap_stop_active(struct cap_stream *s) {
    if (s->active_ring) {
        atomic_store_explicit(&s->active_ring->active, 0, memory_order_release);
        cap_ring_put(s->active_ring);
        s->active_ring = NULL;
    }
    atomic_fetch_add_explicit(&s->gen, 1, memory_order_acq_rel);
}

static int cap_send_status(int fd, struct cap_stream *s, uint32_t code) {
    if (cap_flush_tx(fd, s) < 0) return -1;
    struct mn_audio_msg m = {MN_AUDIO_STATUS, code};
    if (s->tx_off > 0 && s->tx_len + sizeof m > sizeof s->tx_buf) {
        memmove(s->tx_buf, s->tx_buf + s->tx_off, s->tx_len - s->tx_off);
        s->tx_len -= s->tx_off;
        s->tx_off = 0;
    }
    if (s->tx_len + sizeof m > sizeof s->tx_buf) {
        struct pollfd p = {.fd = fd, .events = POLLOUT};
        if (poll(&p, 1, 200) <= 0 || cap_flush_tx(fd, s) < 0) return -1;
        if (s->tx_off > 0) {
            memmove(s->tx_buf, s->tx_buf + s->tx_off, s->tx_len - s->tx_off);
            s->tx_len -= s->tx_off;
            s->tx_off = 0;
        }
        if (s->tx_len + sizeof m > sizeof s->tx_buf) return -1;
    }
    memcpy(s->tx_buf + s->tx_len, &m, sizeof m);
    s->tx_len += sizeof m;
    return cap_flush_tx(fd, s);
}

static int cap_drain_ring(int fd, struct cap_stream *s) {
    if (cap_flush_tx(fd, s) < 0) return -1;
    if (s->tx_off < s->tx_len) return 0; /* wait for POLLOUT */
    struct cap_ring *ring = s->active_ring;
    if (!ring) return 0;
    uint64_t max_chunk = CAP_TX_CHUNK - (CAP_TX_CHUNK % ring->frame);
    while (s->active_ring == ring && atomic_load_explicit(&ring->active, memory_order_relaxed)) {
        uint64_t w = atomic_load_explicit(&ring->w, memory_order_acquire);
        uint64_t r = atomic_load_explicit(&ring->r, memory_order_relaxed);
        uint64_t avail = w - r;
        if (avail < ring->frame) break;
        uint64_t chunk = avail < max_chunk ? avail : max_chunk;
        chunk -= chunk % ring->frame;
        struct mn_audio_msg hdr = {MN_AUDIO_DATA, (uint32_t)chunk};
        memcpy(s->tx_buf, &hdr, sizeof hdr);
        uint64_t at = r & (ring->size - 1);
        uint64_t first = chunk < ring->size - at ? chunk : ring->size - at;
        memcpy(s->tx_buf + sizeof hdr, ring->buf + at, (size_t)first);
        if (chunk > first) memcpy(s->tx_buf + sizeof hdr + first, ring->buf, (size_t)(chunk - first));
        atomic_store_explicit(&ring->r, r + chunk, memory_order_release);
        s->tx_off = 0;
        s->tx_len = sizeof hdr + (size_t)chunk;
        s->sent_bytes += chunk;
        if (cap_flush_tx(fd, s) < 0) return -1;
        if (s->tx_off < s->tx_len) break; /* socket buffer full; resume on POLLOUT */
    }
    return 0;
}

static int cap_handle_start(int fd, struct cap_stream *s) {
    /* Stop any prior session generation and drop its ring first. */
    cap_stop_active(s);
    uint64_t gen = atomic_load_explicit(&s->gen, memory_order_acquire);

    const char *env = getenv("MN_AUDIO_INPUT");
    if (env && !strcmp(env, "none")) {
        fprintf(stderr, "mn-audio: capture stream %u: start mode=none\n", s->id);
        return cap_send_status(fd, s, MN_AUDIO_ERR_NODEV);
    }
    if (env && !strcmp(env, "denied")) {
        fprintf(stderr, "mn-audio: capture stream %u: start mode=denied\n", s->id);
        return cap_send_status(fd, s, MN_AUDIO_ERR_DENIED);
    }

    enum cap_mode mode = CAP_MODE_REAL;
    int auth = MN_MIC_AUTHORIZED;
    if (env && !strcmp(env, "hang")) {
        mode = CAP_MODE_HANG;
        fprintf(stderr, "mn-audio: capture stream %u: start mode=hang\n", s->id);
    } else if (env && !strcmp(env, "sine")) {
        mode = CAP_MODE_SINE;
        fprintf(stderr, "mn-audio: capture stream %u: start mode=sine\n", s->id);
    } else {
        auth = mn_audio_mic_auth_status();
        if (auth == MN_MIC_DENIED) {
            fprintf(stderr, "mn-audio: capture stream %u: tcc=denied\n", s->id);
            return cap_send_status(fd, s, MN_AUDIO_ERR_DENIED);
        }
    }

    struct cap_ring *ring = mode == CAP_MODE_HANG ? NULL : cap_ring_new(s->ring_size, s->frame);
    struct cap_start_job *job = calloc(1, sizeof *job);
    if (!job || (mode != CAP_MODE_HANG && !ring)) {
        cap_ring_put(ring);
        free(job);
        return cap_send_status(fd, s, MN_AUDIO_ERR_IO);
    }
    job->done = dispatch_semaphore_create(0);
    if (!job->done) {
        cap_ring_put(ring);
        free(job);
        return cap_send_status(fd, s, MN_AUDIO_ERR_IO);
    }
    job->s = s;
    job->ring = ring;
    job->gen = gen;
    job->mode = mode;
    job->auth = auth;
    atomic_init(&job->state, 0);
    atomic_init(&job->status, MN_AUDIO_ERR_TIMEOUT);
    atomic_init(&job->refs, 2);
    atomic_fetch_add_explicit(&s->refs, 1, memory_order_relaxed);

    pthread_t wt;
    if (pthread_create(&wt, NULL, cap_worker, job)) {
        /* Drop worker's ref + caller's ref cleanly. */
        cap_job_put(job);
        cap_job_put(job);
        return cap_send_status(fd, s, MN_AUDIO_ERR_IO);
    }
    pthread_detach(wt);

    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, CAP_START_TIMEOUT_MS * NSEC_PER_MSEC);
    long wait_rc = dispatch_semaphore_wait(job->done, deadline);
    int st;
    if (wait_rc == 0) {
        st = atomic_load_explicit(&job->status, memory_order_acquire);
    } else {
        int expected = 0;
        if (atomic_compare_exchange_strong_explicit(&job->state, &expected, 1, memory_order_acq_rel,
                                                   memory_order_acquire)) {
            atomic_fetch_add_explicit(&s->gen, 1, memory_order_acq_rel);
            st = MN_AUDIO_ERR_TIMEOUT;
        } else {
            /* Worker committed right at the 200 ms boundary; wait briefly for its signal. */
            dispatch_semaphore_wait(job->done, DISPATCH_TIME_FOREVER);
            st = atomic_load_explicit(&job->status, memory_order_acquire);
        }
    }
    if (st == MN_AUDIO_OK) s->active_ring = cap_ring_retain(ring);
    cap_job_put(job);

    if (cap_send_status(fd, s, (uint32_t)st)) return -1;
    if (st == MN_AUDIO_OK) return cap_drain_ring(fd, s);
    return 0;
}

static void *serve_capture(int fd, const struct mn_audio_hello *h, uint32_t buffer_frames) {
    struct cap_stream *s = calloc(1, sizeof *s);
    int wake[2] = {-1, -1};
    if (!s || pipe(wake)) {
        free(s);
        close(fd);
        return NULL;
    }
    for (int i = 0; i < 2; i++) {
        int fl = fcntl(wake[i], F_GETFL, 0);
        if (fl >= 0) fcntl(wake[i], F_SETFL, fl | O_NONBLOCK);
        fcntl(wake[i], F_SETFD, FD_CLOEXEC);
    }
    s->wake_rd = wake[0];
    s->wake_wr = wake[1];
    s->rate = h->rate;
    s->channels = h->channels;
    s->f32 = h->format == MN_AUDIO_F32;
    s->frame = h->channels * (s->f32 ? 4 : 2);
    if (buffer_frames > 192000u * 2u) buffer_frames = 192000u * 2u;
    s->buffer_frames = buffer_frames;
    uint64_t min_bytes = (uint64_t)h->rate * s->frame;
    if ((uint64_t)buffer_frames * s->frame > min_bytes) min_bytes = (uint64_t)buffer_frames * s->frame;
    if (min_bytes > 4u * 1024u * 1024u) min_bytes = 4u * 1024u * 1024u;
    for (s->ring_size = 64 * 1024; s->ring_size < min_bytes; s->ring_size <<= 1) {}
    s->id = atomic_fetch_add(&g_streams, 1) + 1;
    atomic_init(&s->refs, 1);

    fprintf(stderr, "mn-audio: capture stream %u: %u Hz, %u channels, %s\n", s->id, h->rate, h->channels,
            s->f32 ? "float" : "s16");

    for (;;) {
        short fd_events = POLLIN;
        if (s->tx_off < s->tx_len) fd_events |= POLLOUT;
        struct pollfd p[2] = {
            {.fd = fd, .events = fd_events},
            {.fd = s->wake_rd, .events = POLLIN},
        };
        int n = poll(p, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (p[1].revents & POLLIN) {
            char drain_buf[64];
            while (read(s->wake_rd, drain_buf, sizeof drain_buf) > 0) {}
        }
        if (p[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            struct mn_audio_msg m;
            if (read_all(fd, &m, sizeof m)) break;
            if (m.type == MN_AUDIO_START) {
                if (cap_handle_start(fd, s)) break;
            } else if (m.type == MN_AUDIO_PAUSE || m.type == MN_AUDIO_STOP) {
                cap_stop_active(s);
            } else {
                break;
            }
        }
        if ((p[0].revents & POLLOUT) || (p[1].revents & POLLIN)) {
            if (cap_drain_ring(fd, s)) break;
        }
    }

    cap_stop_active(s);
    atomic_store_explicit(&s->closing, 1, memory_order_release);
    fprintf(stderr, "mn-audio: capture stream %u: %llu frames captured\n", s->id,
            (unsigned long long)(s->sent_bytes / s->frame));
    cap_put(s);
    close(fd);
    return NULL;
}

static void *serve(void *arg) {
    int fd = (int)(intptr_t)arg;
    struct mn_audio_hello h;
    uint32_t buffer = 0; /* frames, from an MNA2 hello */
    if (read_all(fd, &h, sizeof h)) {
        fprintf(stderr, "mn-audio: refused a stream\n");
        close(fd);
        return NULL;
    }
    if (h.magic == MN_AUDIO_MAGIC_CAP) {
        if (read_all(fd, &buffer, sizeof buffer) || h.rate < 8000 || h.rate > 192000 ||
            (h.channels != 1 && h.channels != 2) || h.format > MN_AUDIO_F32) {
            fprintf(stderr, "mn-audio: refused a stream\n");
            close(fd);
            return NULL;
        }
        return serve_capture(fd, &h, buffer);
    }

    struct stream *s = calloc(1, sizeof *s);
    pthread_t t;
    if (!s || (h.magic != MN_AUDIO_MAGIC && h.magic != MN_AUDIO_MAGIC2) ||
        (h.magic == MN_AUDIO_MAGIC2 && read_all(fd, &buffer, sizeof buffer)) || h.channels != 2 || h.rate < 8000 ||
        h.rate > 384000 || h.format > MN_AUDIO_F32) {
        fprintf(stderr, "mn-audio: refused a stream\n");
        free(s);
        close(fd);
        return NULL;
    }
    ensure_mute_env();
    int want_mute = !atomic_load_explicit(&g_focus_active, memory_order_relaxed) &&
                    atomic_load_explicit(&g_mute_in_background, memory_order_relaxed);
    s->fade_phase = want_mute ? 0.0f : 1.0f;
    s->rate = h.rate, s->channels = h.channels, s->f32 = h.format == MN_AUDIO_F32;
    uint64_t quarter = (uint64_t)buffer * 1000 / h.rate / 4;
    s->report_ms = !buffer || quarter >= REPORT_MS ? REPORT_MS : quarter < 2 ? 2 : (int)quarter;
    s->frame = h.channels * (s->f32 ? 4 : 2);
    for (s->size = 256 * 1024; s->size < (uint64_t)h.rate * s->frame; s->size <<= 1) {}  /* >= the guest's buffer */
    s->id = atomic_fetch_add(&g_streams, 1) + 1;
    s->refs = 2;
    if (!(s->ring = malloc(s->size)) || pthread_create(&t, NULL, play, s)) {
        free(s->ring);
        free(s);
        close(fd);
        return NULL;
    }
    pthread_detach(t);
    fprintf(stderr, "mn-audio: stream %u: %u Hz, %u channels, %s, the position every %d ms\n", s->id, h.rate,
            h.channels, s->f32 ? "float" : "s16", s->report_ms);
    uint64_t started = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (uint64_t told = 0;;) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int n = poll(&p, 1, s->report_ms);
        s->turns++;
        if (n < 0 && errno != EINTR) break;
        if (n > 0) {
            struct mn_audio_msg m;
            if (read_all(fd, &m, sizeof m)) break;
            s->messages++;
            if (m.type == MN_AUDIO_DATA) {
                if (queue(fd, s, m.bytes, &told)) break;
            } else if (m.type == MN_AUDIO_START || m.type == MN_AUDIO_PAUSE) {
                atomic_store(&s->playing, m.type == MN_AUDIO_START);
            } else if (m.type == MN_AUDIO_STOP) {
                atomic_store(&s->playing, 0);
                atomic_store_explicit(&s->drop_to, atomic_load(&s->w), memory_order_release);
            } else {
                break;
            }
        }
        if (report(fd, s, &told)) break;
    }
    uint64_t dropped = atomic_load(&s->dropped) / s->frame;
    double secs = (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - started) / 1e9;
    fprintf(stderr, "mn-audio: stream %u: %llu frames played, %llu dropped; over %.0f s, %.0f turns, %.0f messages and "
            "%.0f reports a second\n", s->id, (unsigned long long)(atomic_load(&s->r) / s->frame - dropped),
            (unsigned long long)dropped, secs, secs > 0 ? s->turns / secs : 0, secs > 0 ? s->messages / secs : 0,
            secs > 0 ? s->reports / secs : 0);
    atomic_store(&s->closing, 1);
    put(s);
    close(fd);
    return NULL;
}

static void *accept_loop(void *arg) {
    int l = (int)(intptr_t)arg;
    for (;;) {
        int fd = accept(l, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            return NULL;
        }
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);  /* a guest gone mid-send must not end the VM */
        pthread_t t;
        if (pthread_create(&t, NULL, serve, (void *)(intptr_t)fd)) close(fd);
        else pthread_detach(t);
    }
}

int mn_audio_listen(const char *path) {
    ensure_mute_env();
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    if (strlen(path) >= sizeof a.sun_path) return errno = ENAMETOOLONG, -1;
    strcpy(a.sun_path, path);
    int l = socket(AF_UNIX, SOCK_STREAM, 0);
    if (l < 0) return -1;
    unlink(path);
    pthread_t t;
    if (bind(l, (struct sockaddr *)&a, sizeof a) || listen(l, 16) ||
        pthread_create(&t, NULL, accept_loop, (void *)(intptr_t)l)) {
        int e = errno;
        close(l);
        return errno = e, -1;
    }
    pthread_detach(t);
    const char *mode = getenv("MN_AUDIO_OUTPUT");
    if ((!mode || strcmp(mode, "none")) && !pthread_create(&t, NULL, ask_coreaudio, NULL)) pthread_detach(t);
    return 0;
}
