/* The guest's sound (spec §5.7): every connection to the audio socket (the guest's vsock port MN_AUDIO_PORT, from
 * eac-vm/guest/alsa-mn.c; protocol eac-vm/guest/mn-audio.h) is one ALSA stream, played through its own CoreAudio
 * output unit: the Mac's default output, which converts the rate and follows the user's choice of device. A second of
 * the stream's frames (and at least the plugin's largest ALSA buffer) sits in a ring between the connection's thread
 * and the unit's render callback; what the callback takes is what the guest's ALSA sees as played. Without an output,
 * a thread takes frames at the stream's rate instead, so a game never waits on its sound: no device, CoreAudio not
 * answering yet (a stuck device driver can stall every client's calls, so it is asked once, off the streams'
 * threads), or MN_AUDIO_OUTPUT=none (tests). */
#include "audio.h"
#include "../guest/mn-audio.h"
#include <AudioToolbox/AudioToolbox.h>
#include <errno.h>
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

struct stream {
    unsigned id, rate, frame;     /* frame: bytes a frame */
    uint8_t *ring;
    uint64_t size;                /* bytes, a power of two */
    _Atomic uint64_t w, r;        /* bytes queued by the connection; taken (played or dropped) by the output */
    _Atomic uint64_t dropped;     /* bytes */
    _Atomic int playing, drop, closing;
    AudioUnit unit;               /* or none: null_output takes the frames */
    pthread_t null_thread;
};

/* The output's side: whole frames from the ring into dst (if any), silence for the rest. A drop empties the ring. */
static void take(struct stream *s, uint8_t *dst, uint64_t want) {
    uint64_t r = atomic_load_explicit(&s->r, memory_order_relaxed);
    uint64_t w = atomic_load_explicit(&s->w, memory_order_acquire), got = 0;
    int dropping = atomic_load_explicit(&s->drop, memory_order_acquire);
    if (dropping) {
        atomic_fetch_add_explicit(&s->dropped, w - r, memory_order_relaxed);
        r = w;
    } else if (atomic_load_explicit(&s->playing, memory_order_relaxed)) {
        got = w - r < want ? w - r : want;
        got -= got % s->frame;
        uint64_t at = r & (s->size - 1), first = got < s->size - at ? got : s->size - at;
        if (dst) memcpy(dst, s->ring + at, first), memcpy(dst + first, s->ring, got - first);
    }
    if (dst) memset(dst + got, 0, want - got);
    atomic_store_explicit(&s->r, r + got, memory_order_release);
    if (dropping) atomic_store_explicit(&s->drop, 0, memory_order_release);
}

static OSStatus render(void *ref, AudioUnitRenderActionFlags *flags, const AudioTimeStamp *time, UInt32 bus,
                       UInt32 frames, AudioBufferList *out) {
    (void)flags, (void)time, (void)bus, (void)frames;
    struct stream *s = ref;
    take(s, out->mBuffers[0].mData, out->mBuffers[0].mDataByteSize);
    return noErr;
}

static void *null_output(void *arg) {
    struct stream *s = arg;
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint64_t taken = 0;
    while (!atomic_load(&s->closing)) {
        usleep(5000);
        clock_gettime(CLOCK_MONOTONIC, &t);
        uint64_t due = (uint64_t)(((double)(t.tv_sec - t0.tv_sec) + (t.tv_nsec - t0.tv_nsec) / 1e9) * s->rate);
        take(s, NULL, (due - taken) * s->frame);
        taken = due;
    }
    return NULL;
}

static const AudioComponentDescription default_output = {.componentType = kAudioUnitType_Output,
                                                         .componentSubType = kAudioUnitSubType_DefaultOutput,
                                                         .componentManufacturer = kAudioUnitManufacturer_Apple};
static _Atomic int answered; /* CoreAudio made an output once */

static void *ask_coreaudio(void *arg) {
    (void)arg;
    AudioComponent c = AudioComponentFindNext(NULL, &default_output);
    AudioUnit u;
    if (c && !AudioComponentInstanceNew(c, &u)) {
        AudioComponentInstanceDispose(u);
        atomic_store(&answered, 1);
    } else {
        fprintf(stderr, "mn-audio: no audio output\n");
    }
    return NULL;
}

static AudioUnit output(struct stream *s, unsigned channels, int f32) {
    AudioComponent c = atomic_load(&answered) ? AudioComponentFindNext(NULL, &default_output) : NULL;
    AudioUnit u = NULL;
    if (!c || AudioComponentInstanceNew(c, &u)) return NULL;
    AudioStreamBasicDescription f = {
        .mSampleRate = s->rate, .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = (f32 ? kAudioFormatFlagIsFloat : kAudioFormatFlagIsSignedInteger) | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = s->frame, .mFramesPerPacket = 1, .mBytesPerFrame = s->frame,
        .mChannelsPerFrame = channels, .mBitsPerChannel = f32 ? 32 : 16,
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
    return send(fd, &played, sizeof played, 0) == sizeof played ? 0 : -1;
}

/* A DATA message's frames into the ring. The guest never has more than its ALSA buffer out (at most 256 KiB, which
 * the ring holds), so waiting for room is only ever brief. */
static int queue(int fd, struct stream *s, uint32_t bytes, uint64_t *told) {
    while (bytes) {
        uint64_t w = atomic_load_explicit(&s->w, memory_order_relaxed);
        uint64_t room = s->size - (w - atomic_load_explicit(&s->r, memory_order_acquire));
        if (!room) {
            usleep(1000);
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

static void *serve(void *arg) {
    static _Atomic unsigned streams;
    int fd = (int)(intptr_t)arg, null_started = 0;
    struct mn_audio_hello h;
    struct stream *s = calloc(1, sizeof *s);
    if (!s || read_all(fd, &h, sizeof h) || h.magic != MN_AUDIO_MAGIC || h.channels != 2 || h.rate < 8000 ||
        h.rate > 384000 || h.format > MN_AUDIO_F32) {
        fprintf(stderr, "mn-audio: refused a stream\n");
        goto out;
    }
    s->rate = h.rate, s->frame = h.channels * (h.format == MN_AUDIO_F32 ? 4 : 2);
    for (s->size = 256 * 1024; s->size < (uint64_t)h.rate * s->frame; s->size <<= 1) {}  /* >= the guest's buffer */
    if (!(s->ring = malloc(s->size))) goto out;
    s->id = ++streams;
    if (!(s->unit = output(s, h.channels, h.format == MN_AUDIO_F32)))
        null_started = !pthread_create(&s->null_thread, NULL, null_output, s);
    fprintf(stderr, "mn-audio: stream %u: %u Hz, %u channels, %s%s\n", s->id, h.rate, h.channels,
            h.format == MN_AUDIO_F32 ? "float" : "s16", s->unit ? "" : ", no output: played into nothing");
    for (uint64_t told = 0;;) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int n = poll(&p, 1, 2);
        if (n < 0 && errno != EINTR) break;
        if (n > 0) {
            struct mn_audio_msg m;
            if (read_all(fd, &m, sizeof m)) break;
            if (m.type == MN_AUDIO_DATA) {
                if (queue(fd, s, m.bytes, &told)) break;
            } else if (m.type == MN_AUDIO_START || m.type == MN_AUDIO_PAUSE) {
                atomic_store(&s->playing, m.type == MN_AUDIO_START);
            } else if (m.type == MN_AUDIO_STOP) {
                atomic_store(&s->playing, 0);
                atomic_store_explicit(&s->drop, 1, memory_order_release);
                for (int waited = 0; atomic_load_explicit(&s->drop, memory_order_acquire); waited++) {
                    if (waited == 400) take(s, NULL, 0);  /* the output has stalled (200 ms): drop here */
                    else usleep(500);
                }
            } else {
                break;
            }
        }
        if (report(fd, s, &told)) break;
    }
out:
    if (s) {
        atomic_store(&s->closing, 1);
        if (s->unit) {
            AudioOutputUnitStop(s->unit);
            AudioUnitUninitialize(s->unit);
            AudioComponentInstanceDispose(s->unit);
        } else if (null_started) {
            pthread_join(s->null_thread, NULL);
        }
        if (s->id) {
            uint64_t dropped = atomic_load(&s->dropped) / s->frame;
            fprintf(stderr, "mn-audio: stream %u: %llu frames played, %llu dropped\n", s->id,
                    (unsigned long long)(atomic_load(&s->r) / s->frame - dropped), (unsigned long long)dropped);
        }
        free(s->ring);
        free(s);
    }
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
    const char *out = getenv("MN_AUDIO_OUTPUT");
    if ((!out || strcmp(out, "none")) && !pthread_create(&t, NULL, ask_coreaudio, NULL)) pthread_detach(t);
    return 0;
}
