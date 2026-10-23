/* The Metal transport's guest side (see transport.h). A thread's ring gets a fence ring index (1 to 63, leased) for
 * its waits while one is free; threads beyond that poll. Control messages go through EXECBUFFER on index 0. */
#include <drm/virtgpu_drm.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "transport.h"
#include "../ring.h"

struct thread_ring { struct mn_blob blob; struct mn_ring_view v; uint64_t head, seq; int fence_idx, drained; };

static int fd = -1;
static atomic_uint_fast64_t next_blob_id = 1;
static struct mn_blob trap_blob;   /* drained mode's trap: its pages are never mapped on the host */
static volatile uint64_t *trap;
static atomic_uint spin_us = 50;
static pthread_mutex_t idx_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t idx_used = 1; /* bit i: fence ring index i is leased; 0 carries control messages */
static pthread_key_t ring_key;
static __thread struct thread_ring *tr;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* One control message; with fence_fd, fenced on ring index ring_idx and its sync_file returned. */
static int ctrl(uint32_t type, uint64_t ring_blob, uint64_t arg, uint32_t ring_idx, int *fence_fd) {
    struct mn_ctrl c = { .type = type, .ring_blob = ring_blob, .arg = arg };
    struct drm_virtgpu_execbuffer eb = { .size = sizeof c, .command = (uintptr_t)&c, .fence_fd = -1 };
    if (fence_fd) {
        eb.flags = VIRTGPU_EXECBUF_RING_IDX | VIRTGPU_EXECBUF_FENCE_FD_OUT;
        eb.ring_idx = ring_idx;
    }
    if (ioctl(fd, DRM_IOCTL_VIRTGPU_EXECBUFFER, &eb)) return -errno;
    if (fence_fd) *fence_fd = eb.fence_fd;
    return 0;
}

/* Wine's, from the ntdll.so winemetal.so links; weak, as the plain-Linux test programs have none. */
extern int32_t NtAllocateVirtualMemory(void *process, void **addr, uintptr_t zero_bits, size_t *size, uint32_t type,
                                       uint32_t protect) __attribute__((weak));
extern int32_t NtFreeVirtualMemory(void *process, void **addr, size_t *size, uint32_t type) __attribute__((weak));
extern int32_t NtQuerySystemInformation(uint32_t cls, void *info, uint32_t size, uint32_t *ret) __attribute__((weak));
#define CURRENT_PROCESS ((void *)~(uintptr_t)0)
enum { MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000, PAGE_READWRITE = 0x04 };

/* The mask Wine's own unix libraries give a WoW64 process's allocations (win32u, wow64): its 32-bit address space's
 * top, 4 GiB for a large-address-aware program, 2 GiB otherwise (SystemEmulationBasicInformation's HighestUserAddress,
 * at 40 in the 64-bit SYSTEM_BASIC_INFORMATION, which Wine fills only at its exact size). An i386 process (old WoW64)
 * gives none: Wine keeps its allocations inside the program's own address space. */
static uintptr_t zero_bits(void) {
#ifdef __i386__
    return 0;
#else
    static uintptr_t bits; /* the same value whichever thread computes it */
    if (!bits) {
        uint8_t info[64] = {0};
        uint64_t highest = 0;
        if (NtQuerySystemInformation && !NtQuerySystemInformation(62, info, sizeof info, NULL))
            memcpy(&highest, info + 40, sizeof highest);
        bits = (uintptr_t)highest | 0x7fffffff;
    }
    return bits;
#endif
}

/* Memory in the 32-bit address space from Wine's allocator (committed, read-write), or NULL; and its release: 0, or -1
 * when Wine didn't release the range. */
static void *mn_low_alloc(uint64_t size) {
    void *at = NULL;
    size_t n = size;
    if (!NtAllocateVirtualMemory ||
        NtAllocateVirtualMemory(CURRENT_PROCESS, &at, zero_bits(), &n, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
        return NULL;
    return at;
}
static int mn_low_free(void *p) {
    size_t n = 0;
    return !p || (NtFreeVirtualMemory && !NtFreeVirtualMemory(CURRENT_PROCESS, &p, &n, MEM_RELEASE)) ? 0 : -1;
}

static void blob_close(struct mn_blob *b) {
    struct drm_gem_close gc = { .handle = b->gem };
    if (!b->low) munmap(b->ptr, b->size);
    else if (mn_low_free(b->ptr)) /* Wine's release replaces our mapping too; failing that, drop the BO's pages */
        mmap(b->ptr, b->size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
}

static void wait_done(struct thread_ring *t, uint64_t seq);
static uint64_t put(struct thread_ring *t, uint32_t op, const void *p, uint32_t len);
static void flush(struct thread_ring *t) {
    atomic_thread_fence(memory_order_release); /* the published head before the store that exits */
    *trap = t->blob.blob_id;
}

static void thread_exit(void *p) {
    struct thread_ring *t = p;
    /* Drain here, in this thread: a DETACH with records queued would drain them on the host's gpu worker, stalling
     * every other thread's control messages meanwhile. */
    uint64_t seq = put(t, MN_OP_NOP | MN_REC_SYNC, NULL, 0);
    if (t->drained) flush(t);
    else wait_done(t, seq);
    ctrl(MN_CTRL_RING_DETACH, t->blob.blob_id, 0, 0, NULL); /* the host stops its consumer before the close */
    blob_close(&t->blob);
    pthread_mutex_lock(&idx_lock);
    idx_used &= ~(1ull << t->fence_idx);
    pthread_mutex_unlock(&idx_lock);
    free(t);
    tr = NULL;
}

static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;

static int open_locked(void) {
    if (fd >= 0) return 0;
    int f = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (f < 0) return -errno;
    struct drm_virtgpu_context_set_param ps[] = {
        { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, MN_CAPSET_DXMT },
        { VIRTGPU_CONTEXT_PARAM_NUM_RINGS, 64 },
    };
    struct drm_virtgpu_context_init ci = { .num_params = 2, .ctx_set_params = (uintptr_t)ps };
    if (ioctl(f, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &ci)) {
        int e = errno;
        close(f);
        return -e;
    }
    pthread_key_create(&ring_key, thread_exit);
    fd = f;
    return 0;
}

int mn_open(void) {
    pthread_mutex_lock(&open_lock);
    int r = open_locked();
    pthread_mutex_unlock(&open_lock);
    return r;
}

static int blob_alloc(uint64_t size, uint64_t blob_id, struct mn_blob *b, int low) {
    size = (size + MN_PAGE - 1) / MN_PAGE * MN_PAGE;
    struct drm_virtgpu_resource_create_blob cb = {
        .blob_mem = VIRTGPU_BLOB_MEM_HOST3D, .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE, .size = size,
        .blob_id = blob_id,
    };
    if (ioctl(fd, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &cb)) return -errno;
    struct drm_virtgpu_map m = { .handle = cb.bo_handle };
    void *p = MAP_FAILED, *at = low ? mn_low_alloc(size) : NULL;
    if ((!low || at) && !ioctl(fd, DRM_IOCTL_VIRTGPU_MAP, &m))
        p = mmap(at, size, PROT_READ | PROT_WRITE, MAP_SHARED | (at ? MAP_FIXED : 0), fd, (off_t)m.offset);
    if (p == MAP_FAILED) {
        int e = low && !at ? ENOMEM : errno;
        mn_low_free(at);
        struct drm_gem_close gc = { .handle = cb.bo_handle };
        ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
        return -e;
    }
    *b = (struct mn_blob){ .gem = cb.bo_handle, .blob_id = cb.blob_id, .size = size, .ptr = p, .low = at != NULL };
    return 0;
}

int mn_blob_alloc(uint64_t size, struct mn_blob *b) { return blob_alloc(size, atomic_fetch_add(&next_blob_id, 1), b, 0); }
int mn_blob_alloc_low(uint64_t size, struct mn_blob *b) {
    return blob_alloc(size, atomic_fetch_add(&next_blob_id, 1), b, 1);
}

int mn_drained(void) {
    pthread_mutex_lock(&open_lock);
    int r = trap ? 0 : open_locked();
    if (!r && !trap) {
        r = blob_alloc(MN_PAGE, MN_TRAP_BLOB_ID | atomic_fetch_add(&next_blob_id, 1), &trap_blob, 0);
        if (!r) trap = trap_blob.ptr;
    }
    pthread_mutex_unlock(&open_lock);
    return r;
}

static struct thread_ring *ring(void) {
    if (tr) return tr;
    struct thread_ring *t = calloc(1, sizeof *t);
    if (!t || mn_blob_alloc(MN_RING_BLOB_BYTES, &t->blob)) return free(t), NULL;
    mn_ring_view_init(&t->v, t->blob.ptr);
    pthread_mutex_lock(&idx_lock);
    for (int i = 1; i < 64 && !t->fence_idx; i++)
        if (!(idx_used & (1ull << i))) idx_used |= 1ull << i, t->fence_idx = i;
    pthread_mutex_unlock(&idx_lock);
    /* Fenced on ring index 0, which no wait uses: the host has attached (or refused) the ring once it signals. */
    int ffd = -1, attached = 0;
    t->drained = trap != NULL;
    if (!ctrl(MN_CTRL_RING_ATTACH, t->blob.blob_id, (uint64_t)t->drained, 0, &ffd)) {
        struct pollfd pf = { .fd = ffd, .events = POLLIN };
        poll(&pf, 1, 5000);
        close(ffd);
        attached = t->v.hdr->reply_len == MN_ATTACHED;
    }
    if (!attached) {
        pthread_mutex_lock(&idx_lock);
        idx_used &= ~(1ull << t->fence_idx);
        pthread_mutex_unlock(&idx_lock);
        blob_close(&t->blob);
        free(t);
        return NULL;
    }
    pthread_setspecific(ring_key, t);
    return tr = t;
}

static void wake(void *ctx) { ctrl(MN_CTRL_WAKE, ((struct thread_ring *)ctx)->blob.blob_id, 0, 0, NULL); }

static uint64_t put(struct thread_ring *t, uint32_t op, const void *p, uint32_t len) {
    struct mn_rec *r;
    while (!(r = mn_ring_reserve(&t->v, &t->head, len))) { /* full */
        if (t->drained) flush(t); /* nothing else runs it */
        else sched_yield();       /* the consumer is draining it */
    }
    r->op = op;
    r->seq = ++t->seq;
    if (len) memcpy(r + 1, p, len);
    mn_ring_publish(&t->v, t->head, wake, t);
    return t->seq;
}

static uint64_t done_seq(struct thread_ring *t) {
    return atomic_load_explicit(&t->v.hdr->done_seq, memory_order_acquire);
}

static void wait_done(struct thread_ring *t, uint64_t seq) {
    uint64_t until = now_ns() + atomic_load(&spin_us) * 1000ull;
    /* A clock read costs about 270 ns under FEX: check it every 1,024 spins. */
    for (unsigned i = 1; done_seq(t) < seq; i++)
        if (!(i & 1023) && now_ns() >= until) break;
    while (done_seq(t) < seq) {
        int ffd;
        if (t->fence_idx && !ctrl(MN_CTRL_WAIT, t->blob.blob_id, seq, (uint32_t)t->fence_idx, &ffd)) {
            struct pollfd pf = { .fd = ffd, .events = POLLIN };
            poll(&pf, 1, 1000); /* the host completes it once done_seq >= seq; recheck at least every second */
            close(ffd);
        } else {
            usleep(100);
        }
    }
}

void mn_forget(void) {
    if (fd >= 0) close(fd); /* our reference only: the parent keeps its own */
    fd = -1;
    if (tr) pthread_setspecific(ring_key, NULL); /* the parent's ring is not ours to detach */
    tr = NULL;
    trap = NULL; /* the parent context's: a child that wants drained rings asks again */
    idx_used = 1;
    pthread_mutex_init(&idx_lock, NULL);
    pthread_mutex_init(&open_lock, NULL);
}

int mn_notify_wait(uint64_t blob_id, uint64_t seen, int timeout_ms) {
    struct thread_ring *t = ring();
    if (!t) return -ENODEV;
    int ffd;
    if (t->fence_idx && !ctrl(MN_CTRL_NOTIFY_WAIT, blob_id, seen, (uint32_t)t->fence_idx, &ffd)) {
        struct pollfd pf = { .fd = ffd, .events = POLLIN };
        poll(&pf, 1, timeout_ms);
        close(ffd);
    } else {
        usleep(100);
    }
    return 0;
}

/* Op 0 is ring padding and the top bits mark synchronous records and queue operations: not the caller's (but
 * mn_call_flush's queue operations). */
static int bad_op(uint32_t op) { return op == MN_OP_WRAP || (op & (MN_REC_SYNC | MN_REC_SUBMIT)); }

static int queue(uint32_t op, const void *p, uint32_t len, int flush_now) {
    if (bad_op(op)) return -EINVAL;
    struct thread_ring *t = ring();
    if (!t) return -ENODEV;
    if (len > MN_REC_MAX) return -E2BIG;
    put(t, op, p, len);
    if (t->drained && flush_now) flush(t);
    return 0;
}

int mn_send(uint32_t op, const void *p, uint32_t len) { return queue(op, p, len, 1); }
int mn_queue(uint32_t op, const void *p, uint32_t len) { return queue(op, p, len, 0); }

void mn_flush(void) {
    if (tr && tr->drained) flush(tr);
}

int mn_call_flush(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len) {
    if (bad_op(op & ~MN_REC_SUBMIT)) return -EINVAL;
    struct thread_ring *t = ring();
    if (!t) return -ENODEV;
    if (!t->drained) return mn_call(op & ~MN_REC_SUBMIT, p, len, reply, cap, reply_len);
    if (len > MN_REC_MAX) return -E2BIG;
    uint64_t seq = put(t, op | MN_REC_SYNC, p, len);
    flush(t);
    if (done_seq(t) < seq) return -EIO; /* the host stopped this ring, or found it broken */
    uint32_t n = t->v.hdr->reply_len;
    if (reply && n) memcpy(reply, t->v.reply, n < cap ? n : cap);
    if (reply_len) *reply_len = n;
    return t->v.hdr->reply_status;
}

int mn_call(uint32_t op, const void *p, uint32_t len, void *reply, uint32_t cap, uint32_t *reply_len) {
    if (bad_op(op)) return -EINVAL;
    struct thread_ring *t = ring();
    if (!t) return -ENODEV;
    if (t->drained) return mn_call_flush(op, p, len, reply, cap, reply_len);
    if (len > MN_REC_MAX) return -E2BIG;
    wait_done(t, put(t, op | MN_REC_SYNC, p, len));
    uint32_t n = t->v.hdr->reply_len;
    if (reply && n) memcpy(reply, t->v.reply, n < cap ? n : cap);
    if (reply_len) *reply_len = n;
    return t->v.hdr->reply_status;
}

int mn_blob_free(struct mn_blob *b) {
    uint32_t n;
    int r = mn_call(MN_OP_BLOB_RELEASE, &b->blob_id, sizeof b->blob_id, NULL, 0, &n);
    blob_close(b);
    return r;
}

void mn_ring_stats(uint64_t *sent, uint64_t *done) {
    *sent = tr ? tr->seq : 0;
    *done = tr ? done_seq(tr) : 0;
}

struct mn_ring *mn_ring_header(void) { return tr ? tr->v.hdr : NULL; }

int mn_ring_fenced(void) { return tr ? !!tr->fence_idx : -1; }

void mn_set_spin_us(uint32_t us) { atomic_store(&spin_us, us); }
